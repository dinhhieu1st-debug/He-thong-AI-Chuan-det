"""Self-check for a freshly packaged Zigbee OTA file, run by build_ota.ps1.

Fails (non-zero exit) if the header does not match what was requested, or if
the declared totalImageSize disagrees with the file's actual size on disk -
exactly the class of bug this project has already hit once (a stale/mismatched
OTA index entry). No output is trusted blindly: every field is read back out
of the produced bytes.
"""
import struct
import sys

OTA_MAGIC = 0x0BEEF11E


def fail(msg):
    print(f"FAIL: {msg}")
    sys.exit(1)


def main():
    path, mfg_str, image_type_str, expected_version = sys.argv[1:5]
    expected_mfg = int(mfg_str, 0)
    expected_image_type = int(image_type_str, 0)
    expected_version = int(expected_version, 0)

    with open(path, "rb") as f:
        data = f.read()
    size = len(data)

    if size < 56:
        fail(f"file too short to be a valid OTA header ({size} bytes)")

    magic, hdr_version, hdr_length, field_control, mfg, image_type = struct.unpack_from(
        "<IHHHHH", data, 0
    )
    file_version = struct.unpack_from("<I", data, 14)[0]
    total_image_size = struct.unpack_from("<I", data, 52)[0]

    print(f"magic            : 0x{magic:08X}")
    print(f"manufacturerCode : 0x{mfg:04X} ({mfg})")
    print(f"imageType        : {image_type}")
    print(f"fileVersion      : {file_version}")
    print(f"totalImageSize   : {total_image_size}")
    print(f"actual file size : {size}")

    if magic != OTA_MAGIC:
        fail(f"magic 0x{magic:08X} != 0x{OTA_MAGIC:08X}")
    if mfg != expected_mfg:
        fail(f"manufacturerCode 0x{mfg:04X} != expected 0x{expected_mfg:04X}")
    if image_type != expected_image_type:
        fail(f"imageType {image_type} != expected {expected_image_type}")
    if file_version != expected_version:
        fail(f"fileVersion {file_version} != expected {expected_version}")
    if total_image_size != size:
        fail(f"totalImageSize header ({total_image_size}) != actual file size ({size})")

    # Walk sub-elements and confirm the upgrade-image tag (0x0000) is present,
    # starts right after the header, and its declared length does not run
    # past the end of the file.
    pos = hdr_length
    found_upgrade_image = False
    while pos + 6 <= size:
        tag_id, tag_len = struct.unpack_from("<HI", data, pos)
        end = pos + 6 + tag_len
        print(f"sub-element tagId=0x{tag_id:04X} length={tag_len} offset={pos + 6} end={end}")
        if end > size:
            fail(f"sub-element tagId=0x{tag_id:04X} claims to extend past end of file")
        if tag_id == 0x0000:
            found_upgrade_image = True
            gbl_magic = data[pos + 6 : pos + 10]
            if gbl_magic != bytes.fromhex("eb17a603"):
                fail(f"upgrade-image tag does not start with the GBL magic (got {gbl_magic.hex()})")
        pos = end
        if tag_len == 0:
            break

    if not found_upgrade_image:
        fail("no Upgrade Image tag (0x0000) found in the OTA file")
    if pos != size:
        fail(f"sub-elements end at offset {pos} but file is {size} bytes (trailing/truncated data)")

    print("PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
