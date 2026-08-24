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
//     HeartRate     (int16s, bpm)        - 0x8000 = no real data
//     Spo2          (int16u, %)          - 0xFFFF = no real data
//     FlowRatio     (int16u, %, x100)
//     DropRatio     (int16s, %, x100)
//     AlarmBitmap   (bitmap16): bit0-4 = alarm reasons, bit5-8 = per-channel
//                               signal status, bit9-11 = tare/baseline
//                               events (see ALARM_BIT_*/SIGNAL_BIT_*/
//                               EVENT_BIT_* below)
//     WeightG       (int16u, grams)       - raw load cell reading
//     DropsPerMin   (int16u, drops/min)   - raw drop-sensor rate
//     TargetFlowMlH (int16u, ml/hour, WRITABLE) - doctor-set target infusion
//                               rate; write this attribute (or publish to
//                               the z2m .../set topic with
//                               {"target_flow_ml_h": <value>}) to change it
//                               from the HIS Server
//     TargetDropsPerMin (int16u, drops/min, WRITABLE) - same idea, for the
//                               drop-sensor target ({"target_drops_per_min": <value>})
//     TareCommand   (int8u, WRITABLE) - write any nonzero value (or publish
//                               {"reset_tare": true}) to remotely re-zero the
//                               load cell, no physical button needed
//     HrRecalibrateCommand (int8u, WRITABLE) - write any nonzero value (or
//                               publish {"recalibrate_hr_baseline": true}) to
//                               remotely restart the 60s HR baseline capture
//     HrBaselineSecondsRemaining (int8u, seconds) - live countdown while the
//                               60s baseline is being captured, 0 once done/idle
//     HrBaselineBpm (int16u, bpm) - the HR value actually locked in at the
//                               last completed calibration
//     TareEventCount / HrBaselineEventCount (int8u, wraps 255->0) - persistent
//                               counters incremented once per completed tare/
//                               baseline capture. Used INSTEAD of the one-shot
//                               *_just_completed bits to detect completion:
//                               those bits can be missed if the event fires
//                               before Zigbee reporting is configured (e.g.
//                               the very fast auto-tare at boot, which can
//                               race with network join) - a persistent count
//                               can't be missed, since the HIS Server simply
//                               compares against the last count it saw.

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

// bit9-11: one-off / transient EVENTS, not steady-state alarms - surfaced so
// the bed dashboard can show "tare done" / "HR baseline captured" instead of
// the nurse only seeing it in the device's own serial log.
const EVENT_BIT_TARE_IN_PROGRESS      = 0x200;
const EVENT_BIT_TARE_JUST_COMPLETED   = 0x400;
const EVENT_BIT_HR_BASELINE_COMPLETED = 0x800;

// ZCL sentinel for "no real data" - matches ZCL_HR_INVALID /
// ZCL_SPO2_INVALID exactly in app.c (firmware).
const HR_INVALID = -32768; // 0x8000 read as int16s
const SPO2_INVALID = 0xFFFF;

// TsFlags bits, matching zb_report_ts_result() in app.c.
const TS_BIT_READY            = 0x001;
const TS_BIT_ANOMALY          = 0x002;
const TS_BIT_EARLY_WARNING    = 0x004;
const TS_HR_TREND_SHIFT       = 3;     // 2 bits: 0 steady, 1 rising, 2 falling
const TS_DROPS_TREND_SHIFT    = 5;     // 2 bits, same encoding
const TS_BIT_HR_TRUSTED       = 0x080;
const TS_BIT_DROPS_TRUSTED    = 0x100;

/* AI v2. Ba model chay doc lap tren chip, va thiet bi tra loi duoc cau hoi ma
 * y ta hoi dau tien: HONG O DAY TRUYEN HAY HONG O BENH NHAN? Hai su co nay xu
 * tri hoan toan khac nhau.
 *
 * Cac bit nay nam trong phan CON TRONG cua mot attribute gateway da doc san,
 * nen khong phai doi schema ZCL - chi them vai dong giai ma o day.
 *
 * Thieu chung thi server coi thiet bi la doi cu va tu suy dien lay, va moi su
 * co duong truyen se bi day len muc do. */
const TS_ALERT_LEVEL_SHIFT    = 9;      /* 2 bit: 0 binh thuong .. 3 nguy kich */
const TS_BIT_LINE_BRANCH      = 0x0800;
const TS_BIT_PATIENT_BRANCH   = 0x1000;

/* 3 bit cuoi: phan quyet cua loadcell, gui duoi dang state+1 nen 0 = chua ket
 * luan duoc (cua so trong luong 60 giay chua day). "Chua biet" khac "moi thu
 * on", va gop chung se cho ra mau xanh chua xung dang. */
const TS_LINE_STATE_SHIFT     = 13;
// Sent instead of a number when the model cannot vouch for that channel's
// forecast (TS_FORECAST_INVALID in app.c) - shown as null, never as a figure
// the dashboard would label "forecast".
const TS_FORECAST_INVALID = 0xFFFF;

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
                /* Firmware gui PHAN TRAM (ti so x100). Truoc day cho nay chia
                 * them 100 nua, bien no thanh phan so - roi gateway doc bang
                 * get_int_from_json va cat "1.04" thanh 0. Ket qua: mot ca
                 * truyen dung y lenh hien ra 0% va bi to do. */
                result.flow = msg.data.flowRatio;
            }
            if (msg.data.dropRatio !== undefined) {
                result.drop_rate = msg.data.dropRatio;   /* phan tram - xem ghi chu tren */
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
                result.tare_in_progress = (bitmap & EVENT_BIT_TARE_IN_PROGRESS) !== 0;
                result.tare_just_completed = (bitmap & EVENT_BIT_TARE_JUST_COMPLETED) !== 0;
                result.hr_baseline_just_completed = (bitmap & EVENT_BIT_HR_BASELINE_COMPLETED) !== 0;
            }
            if (msg.data.weightG !== undefined) {
                result.weight_g = msg.data.weightG;
            }
            if (msg.data.dropsPerMin !== undefined) {
                result.drops_per_min = msg.data.dropsPerMin;
            }
            if (msg.data.targetFlowMlH !== undefined) {
                result.target_flow_ml_h = msg.data.targetFlowMlH;
            }
            if (msg.data.targetDropsPerMin !== undefined) {
                result.target_drops_per_min = msg.data.targetDropsPerMin;
            }
            if (msg.data.hrBaselineSecondsRemaining !== undefined) {
                result.hr_baseline_seconds_remaining = msg.data.hrBaselineSecondsRemaining;
            }
            if (msg.data.hrBaselineBpm !== undefined) {
                result.hr_baseline_bpm = msg.data.hrBaselineBpm;
            }
            if (msg.data.tareEventCount !== undefined) {
                result.tare_event_count = msg.data.tareEventCount;
            }
            if (msg.data.hrBaselineEventCount !== undefined) {
                result.hr_baseline_event_count = msg.data.hrBaselineEventCount;
            }
            if (msg.data.tsFlags !== undefined) {
                const ts = msg.data.tsFlags;
                result.ts_ready = (ts & TS_BIT_READY) !== 0;
                result.ts_anomaly = (ts & TS_BIT_ANOMALY) !== 0;
                result.ts_early_warning = (ts & TS_BIT_EARLY_WARNING) !== 0;
                result.ts_trend = (ts >> TS_HR_TREND_SHIFT) & 0x3;
                result.drops_trend = (ts >> TS_DROPS_TREND_SHIFT) & 0x3;
                result.hr_forecast_trusted = (ts & TS_BIT_HR_TRUSTED) !== 0;
                result.drops_forecast_trusted = (ts & TS_BIT_DROPS_TRUSTED) !== 0;

                // TsFlags stores the physical LED enum (green/yellow/red =
                // 0/1/2). HIS uses the same 1/2/3 level shown on the OLED.
                const physicalLevel = (ts >> TS_ALERT_LEVEL_SHIFT) & 0x3;
                result.alert_level = physicalLevel;
                result.final_alert_level = Math.min(physicalLevel + 1, 3);
                result.line_branch = (ts & TS_BIT_LINE_BRANCH) !== 0;
                result.patient_branch = (ts & TS_BIT_PATIENT_BRANCH) !== 0;

                const lineState = (ts >> TS_LINE_STATE_SHIFT) & 0x7;
                result.line_state = lineState === 0 ? null : lineState - 1;
            }
            /* Con bao nhieu dich, con bao nhieu phut. 0xFFFF = chua uoc luong
             * duoc, va phai ra null chu khong phai 0: "chua biet" khac "het
             * sach", va bao "con 0 phut" cho mot binh chua treo la thu khien y
             * ta thoi tin cai may. */
            if (msg.data.remainingMl !== undefined) {
                result.remaining_ml = msg.data.remainingMl === TS_FORECAST_INVALID
                    ? null : msg.data.remainingMl;
            }
            // Đang theo dõi hay đang chờ. Ở chế độ chờ, thiết bị không chạy AI
            // và không báo động - bảng điều khiển phải nói rõ điều đó, chứ
            // không được hiện "bình thường" cho một giường chưa hề được theo dõi.
            if (msg.data.monitoringActive !== undefined) {
                result.monitoring = msg.data.monitoringActive !== 0;
            }
            if (msg.data.dropTrainingSamples !== undefined) {
                result.drop_training_samples = msg.data.dropTrainingSamples;
            }
            if (msg.data.vitalsTrainingSamples !== undefined) {
                result.vitals_training_samples = msg.data.vitalsTrainingSamples;
            }
            if (msg.data.alertsArmed !== undefined) {
                result.alerts_armed = msg.data.alertsArmed !== 0;
            }
            if (msg.data.dropIntervalMs !== undefined) result.drop_interval_ms = msg.data.dropIntervalMs;
            if (msg.data.dropEventCount !== undefined) result.drop_event_count = msg.data.dropEventCount;
            if (msg.data.serverDropLevel !== undefined) result.server_drop_level = msg.data.serverDropLevel;
            if (msg.data.vitalsTestMode !== undefined) result.vitals_test_mode = msg.data.vitalsTestMode;
            if (msg.data.aiInputHeartRate !== undefined) {
                result.ai_input_heart_rate = msg.data.aiInputHeartRate === HR_INVALID ? null : msg.data.aiInputHeartRate;
            }
            if (msg.data.aiInputSpo2 !== undefined) {
                result.ai_input_spo2 = msg.data.aiInputSpo2 === SPO2_INVALID ? null : msg.data.aiInputSpo2;
            }
            if (msg.data.vitalsLevel !== undefined) result.vitals_level = msg.data.vitalsLevel;
            if (msg.data.remainingMin !== undefined) {
                result.remaining_min = msg.data.remainingMin === TS_FORECAST_INVALID
                    ? null : msg.data.remainingMin;
            }
            if (msg.data.hrForecast16s !== undefined) {
                result.hr_forecast_16s = msg.data.hrForecast16s === TS_FORECAST_INVALID
                    ? null : msg.data.hrForecast16s;
            }
            if (msg.data.spo2Forecast16s !== undefined) {
                result.spo2_forecast_16s = msg.data.spo2Forecast16s === TS_FORECAST_INVALID
                    ? null : msg.data.spo2Forecast16s;
            }
            if (msg.data.dropsForecast16s !== undefined) {
                result.drops_forecast_16s = msg.data.dropsForecast16s === TS_FORECAST_INVALID
                    ? null : msg.data.dropsForecast16s;
            }
            if (msg.data.hrTrendBpmPerMin !== undefined) {
                result.hr_trend_bpm_per_min = msg.data.hrTrendBpmPerMin;
            }
            if (msg.data.dropsTrendDpmPerMin !== undefined) {
                result.drops_trend_dpm_per_min = msg.data.dropsTrendDpmPerMin;
            }
            if (msg.data.tsAnomalyScoreX100 !== undefined) {
                // Server field name says X100 for the same reason it is sent
                // that way: an integer wire format that keeps two decimals.
                result.ts_anomaly_score = msg.data.tsAnomalyScoreX100;
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
    //
    // TWO entries on purpose. zigbee-herdsman-converters indexes an external
    // definition by `fingerprint.modelID` (falling back to the literal key
    // "null" when absent) and only ever looks at candidates found under the
    // device's own modelID - the endpoint signature below is checked *after*
    // that lookup, never instead of it. So an entry without `modelID` is only
    // reachable while the interview leaves modelID empty; the moment the Basic
    // read succeeds and the device reports "SmartIV-Sensor", the lookup misses
    // and z2m logs "Not supported" despite a perfectly matching signature.
    // Keep both so either interview outcome resolves.
    fingerprint: [{
        modelID: 'SmartIV-Sensor',
        endpoints: [
            {ID: 1, inputClusters: [0]},
            {ID: 2, inputClusters: [SMART_IV_CLUSTER_ID]},
        ],
    }, {
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
                heartRate:     {name: 'heartRate',     ID: 0x0000, type: 0x29}, // int16s
                spo2:          {name: 'spo2',          ID: 0x0001, type: 0x21}, // int16u
                flowRatio:     {name: 'flowRatio',     ID: 0x0002, type: 0x21}, // int16u
                dropRatio:     {name: 'dropRatio',     ID: 0x0003, type: 0x29}, // int16s
                alarmBitmap:   {name: 'alarmBitmap',   ID: 0x0004, type: 0x19}, // bitmap16
                weightG:              {name: 'weightG',              ID: 0x0005, type: 0x21}, // int16u
                dropsPerMin:          {name: 'dropsPerMin',          ID: 0x0006, type: 0x21}, // int16u
                // NOTE: zigbee-herdsman refuses to even ATTEMPT an over-the-air
                // write unless `write: true` is set here - without it, entity.write()
                // throws a client-side "NOT_AUTHORIZED ... is not writable" error
                // before ever sending anything (the firmware/device is never even
                // contacted). Every attribute the doctor can set from the HIS
                // Server needs this flag.
                targetFlowMlH:        {name: 'targetFlowMlH',        ID: 0x0007, type: 0x21, write: true}, // int16u, writable
                targetDropsPerMin:    {name: 'targetDropsPerMin',    ID: 0x0008, type: 0x21, write: true}, // int16u, writable
                tareCommand:          {name: 'tareCommand',          ID: 0x0009, type: 0x20, write: true}, // int8u, writable (fire)
                hrRecalibrateCommand: {name: 'hrRecalibrateCommand', ID: 0x000A, type: 0x20, write: true}, // int8u, writable (fire)
                hrBaselineSecondsRemaining: {name: 'hrBaselineSecondsRemaining', ID: 0x000B, type: 0x20}, // int8u
                hrBaselineBpm:              {name: 'hrBaselineBpm',              ID: 0x000C, type: 0x21}, // int16u
                tareEventCount:             {name: 'tareEventCount',             ID: 0x000D, type: 0x20}, // int8u
                hrBaselineEventCount:       {name: 'hrBaselineEventCount',       ID: 0x000E, type: 0x20}, // int8u
                // On-chip time-series forecaster (ts_monitor.c). Until these
                // existed its output reached the server ONLY over the serial
                // fallback gateway, so the dashboard's "AI forecast (on-chip)"
                // card never left "Collecting the first 64 seconds..." on a bed
                // running through the Pi.
                remainingMl:          {name: 'remainingMl',          ID: 0x0016, type: 0x21}, // int16u
                remainingMin:         {name: 'remainingMin',         ID: 0x0017, type: 0x21}, // int16u
                monitoringActive:     {name: 'monitoringActive',     ID: 0x0018, type: 0x20, write: true}, // int8u, writable (bat/tat theo doi)
                tsFlags:              {name: 'tsFlags',              ID: 0x000F, type: 0x19}, // bitmap16
                hrForecast16s:        {name: 'hrForecast16s',        ID: 0x0010, type: 0x21}, // int16u
                spo2Forecast16s:      {name: 'spo2Forecast16s',      ID: 0x0011, type: 0x21}, // int16u
                hrTrendBpmPerMin:     {name: 'hrTrendBpmPerMin',     ID: 0x0012, type: 0x29}, // int16s
                tsAnomalyScoreX100:   {name: 'tsAnomalyScoreX100',   ID: 0x0013, type: 0x21}, // int16u
                dropsForecast16s:     {name: 'dropsForecast16s',     ID: 0x0014, type: 0x21}, // int16u
                dropsTrendDpmPerMin:  {name: 'dropsTrendDpmPerMin',  ID: 0x0015, type: 0x29}, // int16s
                dropTrainingSamples:  {name: 'dropTrainingSamples',  ID: 0x0019, type: 0x20}, // int8u
                vitalsTrainingSamples:{name: 'vitalsTrainingSamples',ID: 0x001A, type: 0x20}, // int8u
                alertsArmed:          {name: 'alertsArmed',          ID: 0x001B, type: 0x20}, // int8u
                dropIntervalMs:       {name: 'dropIntervalMs',       ID: 0x001C, type: 0x21}, // int16u
                dropEventCount:       {name: 'dropEventCount',       ID: 0x001D, type: 0x21}, // int16u
                serverDropLevel:      {name: 'serverDropLevel',      ID: 0x001E, type: 0x20, write: true}, // int8u
                vitalsTestMode:       {name: 'vitalsTestMode',       ID: 0x001F, type: 0x20, write: true}, // 0 real, 2 attention, 3 alarm
                aiInputHeartRate:     {name: 'aiInputHeartRate',     ID: 0x0020, type: 0x29}, // int16s
                aiInputSpo2:          {name: 'aiInputSpo2',          ID: 0x0021, type: 0x21}, // int16u
                vitalsLevel:          {name: 'vitalsLevel',          ID: 0x0022, type: 0x20}, // int8u
            },
            commands: {},
            commandsResponse: {},
        }),
    ],
    fromZigbee: [
        fzSmartIv.smart_iv_vitals,
    ],
    toZigbee: [
        {
            key: ['vitals_test_mode'],
            convertSet: async (entity, key, value, meta) => {
                const numeric = Number(value);
                if (![0, 2, 3].includes(numeric)) throw new Error('vitals_test_mode must be 0, 2 or 3');
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {vitalsTestMode: numeric},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {vitals_test_mode: numeric}};
            },
        },
        {
            key: ['server_drop_level'],
            convertSet: async (entity, key, value, meta) => {
                const numeric = Math.max(1, Math.min(3, Number(value)));
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {serverDropLevel: numeric},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {server_drop_level: numeric}};
            },
        },
        {
            // NOTE: `entity` here defaults to the device's first/root endpoint
            // (endpoint 1, the Basic cluster) since this expose isn't scoped to
            // a specific endpoint - but the Smart IV Vitals cluster only exists
            // on endpoint 2. Writing via `entity` silently targets the wrong
            // endpoint and just times out (no ZCL error, since endpoint 1 simply
            // never responds to a cluster it doesn't have). Always resolve
            // endpoint 2 explicitly via meta.device.
            key: ['target_flow_ml_h'],
            convertSet: async (entity, key, value, meta) => {
                const numeric = Number(value);
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {targetFlowMlH: numeric},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {target_flow_ml_h: numeric}};
            },
            convertGet: async (entity, key, meta) => {
                await meta.device.getEndpoint(2).read(SMART_IV_CLUSTER_NAME, ['targetFlowMlH'], {manufacturerCode: SMART_IV_MFG_CODE});
            },
        },
        {
            key: ['target_drops_per_min'],
            convertSet: async (entity, key, value, meta) => {
                const numeric = Number(value);
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {targetDropsPerMin: numeric},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {target_drops_per_min: numeric}};
            },
            convertGet: async (entity, key, meta) => {
                await meta.device.getEndpoint(2).read(SMART_IV_CLUSTER_NAME, ['targetDropsPerMin'], {manufacturerCode: SMART_IV_MFG_CODE});
            },
        },
        {
            // Fire-and-forget trigger: publish {"reset_tare": true} (or any
            // truthy value) to remotely re-zero the load cell - no physical
            // BTN0 press needed. The firmware resets the attribute back to 0
            // itself right after acting on it, so no state is reported back.
            key: ['reset_tare'],
            convertSet: async (entity, key, value, meta) => {
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {tareCommand: 1},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {}};
            },
        },
        {
            // Bật/tắt theo dõi: publish {"monitoring": true} hoặc false.
            // Khác hai lệnh dưới ở chỗ đây là TRẠNG THÁI chứ không phải lệnh
            // bắn-rồi-quên, nên thiết bị giữ nguyên giá trị và báo ngược lên.
            key: ['monitoring'],
            convertSet: async (entity, key, value, meta) => {
                const on = (value === true || value === 'true' || value === 1 || value === 'ON');
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {monitoringActive: on ? 1 : 0},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {monitoring: on}};
            },
        },
        {
            // Fire-and-forget trigger: publish {"recalibrate_hr_baseline": true}
            // to remotely restart the 60s HR baseline capture window.
            key: ['recalibrate_hr_baseline'],
            convertSet: async (entity, key, value, meta) => {
                await meta.device.getEndpoint(2).write(
                    SMART_IV_CLUSTER_NAME,
                    {hrRecalibrateCommand: 1},
                    {manufacturerCode: SMART_IV_MFG_CODE},
                );
                return {state: {}};
            },
        },
    ],
    exposes: [
        e.numeric('heart_rate', ea.STATE).withUnit('bpm').withDescription('Heart rate'),
        e.numeric('spo2', ea.STATE).withUnit('%').withDescription('Blood oxygen saturation'),
        e.numeric('flow', ea.STATE).withUnit('%').withDescription('Flow rate / target ratio'),
        e.numeric('drop_rate', ea.STATE).withUnit('%').withDescription('Drops per minute / target ratio'),
        e.numeric('weight_g', ea.STATE).withUnit('g').withDescription('Raw load cell reading (IV bag weight)'),
        e.numeric('drops_per_min', ea.STATE).withUnit('dpm').withDescription('Raw drop sensor rate'),
        e.numeric('target_flow_ml_h', ea.ALL).withUnit('ml/h').withValueMin(0).withValueMax(1000)
            .withDescription('Doctor-set target infusion rate - settable from the HIS Server'),
        e.numeric('target_drops_per_min', ea.ALL).withUnit('dpm').withValueMin(1).withValueMax(240)
            .withDescription('Doctor-set target drop rate - settable from the HIS Server'),
        e.numeric('hr_baseline_seconds_remaining', ea.STATE).withUnit('s')
            .withDescription('Seconds left in the 60s HR baseline capture window, 0 when idle/done'),
        e.numeric('hr_baseline_bpm', ea.STATE).withUnit('bpm')
            .withDescription('HR value locked in at the last completed baseline capture'),
        e.numeric('tare_event_count', ea.STATE)
            .withDescription('Persistent count of completed tares - used to detect a new completion server-side'),
        e.numeric('hr_baseline_event_count', ea.STATE)
            .withDescription('Persistent count of completed HR baseline captures - used to detect a new completion server-side'),
        e.binary('monitoring', ea.ALL, true, false)
            .withDescription('Monitoring armed. When false the device reads sensors but runs no AI and raises no alarms.'),
        e.binary('reset_tare', ea.SET, true, false)
            .withDescription('Write true to remotely re-zero the load cell (no physical button needed)'),
        e.binary('recalibrate_hr_baseline', ea.SET, true, false)
            .withDescription('Write true to remotely restart the 60s HR baseline capture'),
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
        e.binary('tare_in_progress', ea.STATE, true, false).withDescription('Loadcell tare in progress'),
        e.binary('tare_just_completed', ea.STATE, true, false).withDescription('Loadcell tare just completed (one-shot)'),
        e.binary('hr_baseline_just_completed', ea.STATE, true, false).withDescription('HR 60s baseline sample just completed (one-shot)'),
        e.binary('ts_ready', ea.STATE, true, false).withDescription('Forecaster has collected its first 64s window'),
        e.binary('ts_anomaly', ea.STATE, true, false).withDescription('Persistence-confirmed forecast anomaly'),
        e.binary('ts_early_warning', ea.STATE, true, false).withDescription('Forecast crosses a clinical limit within 16s'),
        e.numeric('alert_level', ea.STATE).withDescription('Raw LED enum: 0 green, 1 yellow, 2 red'),
        e.numeric('final_alert_level', ea.STATE).withDescription('Final G26 severity: 1 normal, 2 attention, 3 alarm'),
        e.numeric('drop_training_samples', ea.STATE).withDescription('Drop AI training progress, 0 to 20 samples'),
        e.numeric('vitals_training_samples', ea.STATE).withDescription('Vitals AI training progress, 0 to 64 samples'),
        e.binary('alerts_armed', ea.STATE, true, false).withDescription('G26 has completed training and enabled alert decisions'),
        e.numeric('drop_interval_ms', ea.STATE).withUnit('ms').withDescription('Latest physical interval between drops'),
        e.numeric('drop_event_count', ea.STATE).withDescription('Monotonic physical drop event counter'),
        e.numeric('server_drop_level', ea.ALL).withValueMin(1).withValueMax(3)
            .withDescription('Server drip decision written back to G26 for final fusion'),
        e.numeric('vitals_test_mode', ea.ALL).withValueMin(0).withValueMax(3)
            .withDescription('Allowed values: 0 real sensors, 2 attention test, 3 alarm test'),
        e.numeric('ai_input_heart_rate', ea.STATE).withUnit('bpm')
            .withDescription('HR value actually supplied to the on-device AI'),
        e.numeric('ai_input_spo2', ea.STATE).withUnit('%')
            .withDescription('SpO2 value actually supplied to the on-device AI'),
        e.numeric('vitals_level', ea.STATE).withValueMin(1).withValueMax(3)
            .withDescription('On-device vital-sign branch level'),
        e.binary('line_branch', ea.STATE, true, false).withDescription('The infusion line is at fault'),
        e.binary('patient_branch', ea.STATE, true, false).withDescription('The patient is at fault'),
        e.numeric('line_state', ea.STATE).withDescription('Load cell verdict: 0 ok, 1 running low, 2 occlusion, 3 free flow, 4 drop-sensor fault, 5 empty'),
        e.numeric('remaining_ml', ea.STATE).withUnit('mL').withDescription('Estimated fluid left in the bag'),
        e.numeric('remaining_min', ea.STATE).withUnit('min').withDescription('Estimated minutes left at the current rate'),
        e.numeric('ts_trend', ea.STATE).withDescription('Heart-rate trend: 0 steady, 1 rising, 2 falling'),
        e.numeric('drops_trend', ea.STATE).withDescription('Drop-rate trend: 0 steady, 1 rising, 2 falling'),
        e.numeric('hr_forecast_16s', ea.STATE).withUnit('bpm').withDescription('Forecast heart rate 16s ahead'),
        e.numeric('spo2_forecast_16s', ea.STATE).withUnit('%').withDescription('Forecast SpO2 16s ahead'),
        e.numeric('drops_forecast_16s', ea.STATE).withUnit('dpm').withDescription('Forecast drops per minute 16s ahead'),
        e.numeric('hr_trend_bpm_per_min', ea.STATE).withUnit('bpm/min').withDescription('Heart-rate slope over the forecast'),
        e.numeric('drops_trend_dpm_per_min', ea.STATE).withUnit('dpm/min').withDescription('Drop-rate slope over the forecast'),
        e.numeric('ts_anomaly_score', ea.STATE).withDescription('Forecast error score x100 (threshold 561)'),
        e.binary('hr_forecast_trusted', ea.STATE, true, false)
            .withDescription('False = the figure is "the normal level there should be", not a forecast'),
        e.binary('drops_forecast_trusted', ea.STATE, true, false)
            .withDescription('False = the figure is "the normal level there should be", not a forecast'),
    ],

    /* Cho phep zigbee2mqtt chao ban cap nhat firmware cho thiet bi nay.
     *
     * Khong co dong nay thi z2m khong bao gio chao gi ca - da kiem tra tren
     * bridge that: ca hai thiet bi deu bao ota=None.
     *
     * Anh nao duoc chao thi do file index quyet dinh (xem
     * gateway/ota/README.md), va index CHI liet ke anh dung tu chinh firmware
     * cua du an. Ma nha san xuat cua thiet bi nay la 0x1049, TRUNG voi may anh
     * thu nghiem co san tren nhanh cu - de mot trong so do lot vao index la ghi
     * de firmware AI cua giuong benh. */
    ota: true,
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
