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
        e.numeric('target_drops_per_min', ea.ALL).withUnit('dpm').withValueMin(0).withValueMax(200)
            .withDescription('Doctor-set target drop rate - settable from the HIS Server'),
        e.numeric('hr_baseline_seconds_remaining', ea.STATE).withUnit('s')
            .withDescription('Seconds left in the 60s HR baseline capture window, 0 when idle/done'),
        e.numeric('hr_baseline_bpm', ea.STATE).withUnit('bpm')
            .withDescription('HR value locked in at the last completed baseline capture'),
        e.numeric('tare_event_count', ea.STATE)
            .withDescription('Persistent count of completed tares - used to detect a new completion server-side'),
        e.numeric('hr_baseline_event_count', ea.STATE)
            .withDescription('Persistent count of completed HR baseline captures - used to detect a new completion server-side'),
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
