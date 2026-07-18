// External converter for zigbee2mqtt — for the "Smart IV Sensor" device
// (project empty_2, running on EFR32xG26, BRD2709A).
//
// Usage: copy this file into zigbee2mqtt's data directory on the Pi/host
// (usually ~/.zigbee2mqtt/ or /opt/zigbee2mqtt/data/), then add to
// configuration.yaml:
//
//   external_converters:
//     - zigbee2mqtt_smart_iv_converter.js
//
// then restart zigbee2mqtt. When pairing the device it will show up as
// model "SmartIV-Sensor" / vendor "ICTU" thanks to the manufacturerName/
// modelId attributes already written into the Basic cluster (endpoint 1)
// on the firmware side.
//
// Wire format (a REAL custom cluster now, no longer "borrowing" a standard
// cluster - see config/zcl/smart-iv-vitals.xml and app.c on the firmware side):
//   EP2 cluster "Smart IV Vitals" (0xFC01, mfgCode 0x1049):
//     HeartRate   (int16s, bpm)        - 0x8000 = no real data
//     Spo2        (int16u, %)          - 0xFFFF = no real data
//     FlowRatio   (int16u, %, x100)
//     DropRatio   (int16s, %, x100)
//     AlarmBitmap (bitmap16): bit0-4 = alarm reasons, bit5-8 = per-channel
//                             signal status (see ALARM_BIT_*/SIGNAL_BIT_* below)

const {deviceAddCustomCluster} = require('zigbee-herdsman-converters/lib/modernExtend');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;
const ea = exposes.access;

const SMART_IV_CLUSTER_NAME = 'smartIvVitals';
const SMART_IV_CLUSTER_ID = 0xFC01;
const SMART_IV_MFG_CODE = 0x1049;

const ALARM_BIT_MISSING = 0x01;
const ALARM_BIT_SPO2    = 0x02;
const ALARM_BIT_HR      = 0x04;
const ALARM_BIT_FLOW    = 0x08;
const ALARM_BIT_AE      = 0x10;

// bit5-8: CONNECTION status of each sensor channel (1 = has signal/CH_OK).
// Independent of the alarm bits above - a channel can "have signal" while
// still alarming (e.g. abnormal heart rate), or "not connected" (always 0).
const SIGNAL_BIT_HR    = 0x20;
const SIGNAL_BIT_SPO2  = 0x40;
const SIGNAL_BIT_FLOW  = 0x80;
const SIGNAL_BIT_DROPS = 0x100;

// ZCL sentinel for "no real data" - matches ZCL_HR_INVALID /
// ZCL_SPO2_INVALID exactly in app.c (firmware).
const HR_INVALID = -32768; // 0x8000 read as int16s
const SPO2_INVALID = 0xFFFF;

const fzSmartIv = {
    smart_iv_vitals: {
        cluster: SMART_IV_CLUSTER_NAME,
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};

            if (msg.data.heartRate !== undefined) {
                result.heart_rate = msg.data.heartRate === HR_INVALID ? null : msg.data.heartRate;
            }
            if (msg.data.spo2 !== undefined) {
                result.spo2 = msg.data.spo2 === SPO2_INVALID ? null : msg.data.spo2;
            }
            if (msg.data.flowRatio !== undefined) {
                result.flow = msg.data.flowRatio / 100;
            }
            if (msg.data.dropRatio !== undefined) {
                result.drop_rate = msg.data.dropRatio / 100;
            }
            if (msg.data.alarmBitmap !== undefined) {
                const bitmap = msg.data.alarmBitmap;
                result.alarm = (bitmap & 0x1F) !== 0;
                result.signal_lost = (bitmap & ALARM_BIT_MISSING) !== 0;
                result.spo2_low = (bitmap & ALARM_BIT_SPO2) !== 0;
                result.heart_rate_abnormal = (bitmap & ALARM_BIT_HR) !== 0;
                result.line_blocked = (bitmap & ALARM_BIT_FLOW) !== 0;
                result.ae_alarm = (bitmap & ALARM_BIT_AE) !== 0;
                result.hr_signal = (bitmap & SIGNAL_BIT_HR) !== 0;
                result.spo2_signal = (bitmap & SIGNAL_BIT_SPO2) !== 0;
                result.flow_signal = (bitmap & SIGNAL_BIT_FLOW) !== 0;
                result.drops_signal = (bitmap & SIGNAL_BIT_DROPS) !== 0;
            }

            return result;
        },
    },
};

const definition = {
    // modelID/manufacturerName over the Basic cluster aren't reliably
    // readable during interview (device.modelID = undefined), so we
    // fingerprint on the exact endpoint/cluster "signature" declared in the
    // empty_2 firmware's ZAP config instead of relying on the zigbeeModel string.
    fingerprint: [{
        endpoints: [
            {ID: 1, inputClusters: [0]},
            {ID: 2, inputClusters: [SMART_IV_CLUSTER_ID]},
        ],
    }],
    model: 'SmartIV-Sensor',
    vendor: 'ICTU',
    description: 'Smart IV drip monitor (AI Smart IV, EFR32xG26)',
    extend: [
        deviceAddCustomCluster(SMART_IV_CLUSTER_NAME, {
            name: SMART_IV_CLUSTER_NAME,
            ID: SMART_IV_CLUSTER_ID,
            manufacturerCode: SMART_IV_MFG_CODE,
            attributes: {
                heartRate:   {name: 'heartRate',   ID: 0x0000, type: 0x29}, // int16s
                spo2:        {name: 'spo2',        ID: 0x0001, type: 0x21}, // int16u
                flowRatio:   {name: 'flowRatio',   ID: 0x0002, type: 0x21}, // int16u
                dropRatio:   {name: 'dropRatio',   ID: 0x0003, type: 0x29}, // int16s
                alarmBitmap: {name: 'alarmBitmap', ID: 0x0004, type: 0x19}, // bitmap16
            },
            commands: {},
            commandsResponse: {},
        }),
    ],
    fromZigbee: [
        fzSmartIv.smart_iv_vitals,
    ],
    toZigbee: [],
    exposes: [
        e.numeric('heart_rate', ea.STATE).withUnit('bpm').withDescription('Heart rate'),
        e.numeric('spo2', ea.STATE).withUnit('%').withDescription('Blood oxygen saturation'),
        e.numeric('flow', ea.STATE).withUnit('%').withDescription('Flow rate / target ratio'),
        e.numeric('drop_rate', ea.STATE).withUnit('%').withDescription('Drops per minute / target ratio'),
        e.binary('alarm', ea.STATE, true, false).withDescription('Combined alarm flag'),
        e.binary('signal_lost', ea.STATE, true, false).withDescription('Signal lost'),
        e.binary('spo2_low', ea.STATE, true, false).withDescription('Low SpO2'),
        e.binary('heart_rate_abnormal', ea.STATE, true, false).withDescription('Abnormal heart rate'),
        e.binary('line_blocked', ea.STATE, true, false).withDescription('Infusion line blocked/free-flow'),
        e.binary('ae_alarm', ea.STATE, true, false).withDescription('Autoencoder threshold exceeded'),
        e.binary('hr_signal', ea.STATE, true, false).withDescription('Heart rate channel has signal'),
        e.binary('spo2_signal', ea.STATE, true, false).withDescription('SpO2 channel has signal'),
        e.binary('flow_signal', ea.STATE, true, false).withDescription('Scale/flow channel has signal'),
        e.binary('drops_signal', ea.STATE, true, false).withDescription('Drop channel has signal'),
    ],
    configure: async (device, coordinatorEndpoint, logger) => {
        // The firmware already configures its own reporting (interval/threshold)
        // locally as soon as it joins the network (see zb_configure_reporting()
        // in app.c) - here we only need to create the BINDING so the firmware
        // knows where to send its reports. Not calling ZCL configureReporting()
        // since that command tends to time out.
        try {
            const ep = device.getEndpoint(2);
            await reporting.bind(ep, coordinatorEndpoint, [SMART_IV_CLUSTER_NAME]);
        } catch (error) {
            logger.warning(`SmartIV-Sensor: bind endpoint 2 (${SMART_IV_CLUSTER_NAME}) failed: ${error.message}`);
        }
    },
    meta: {},
};

module.exports = definition;
