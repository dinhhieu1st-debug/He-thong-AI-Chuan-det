// External converter cho zigbee2mqtt — dan cho thiet bi "Smart IV Sensor"
// (project empty_2, chay tren EFR32xG26, BRD2709A).
//
// Cach dung: copy file nay vao thu muc data cua zigbee2mqtt tren Pi/host
// (thuong la ~/.zigbee2mqtt/ hoac /opt/zigbee2mqtt/data/), sau do trong
// configuration.yaml them:
//
//   external_converters:
//     - zigbee2mqtt_smart_iv_converter.js
//
// roi restart zigbee2mqtt. Khi ghep noi (pair) thiet bi se hien ten model
// "SmartIV-Sensor" / vendor "ICTU" nho attribute manufacturerName/modelId
// da duoc ghi san trong cluster Basic (endpoint 1) cua firmware.
//
// Wire format (endpoint -> cluster muon de cho MeasuredValue):
//   EP2 Temperature Measurement        -> heart_rate (bpm, so nguyen)
//   EP3 Relative Humidity Measurement  -> spo2 (%, so nguyen)
//   EP4 Flow Measurement               -> flow (%, MeasuredValue = %*100)
//   EP5 Pressure Measurement           -> drop_rate (%, MeasuredValue = %*100)
//   EP6 Illuminance Measurement        -> bitmap: bit0-4 = alarm reasons,
//                                         bit5-8 = per-channel signal present
//                                         (HR/SpO2/Flow(weight)/Drops)

const {deviceEndpoints, quirkCheckinInterval} = require('zigbee-herdsman-converters/lib/modernExtend');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee').default;
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const e = exposes.presets;
const ea = exposes.access;

const ALARM_BIT_MISSING = 0x01;
const ALARM_BIT_SPO2    = 0x02;
const ALARM_BIT_HR      = 0x04;
const ALARM_BIT_FLOW    = 0x08;
const ALARM_BIT_AE      = 0x10;

// bit5-8: trang thai KET NOI tung kenh cam bien (1 = co tin hieu/CH_OK).
// Doc lap voi cac bit alarm o tren - mot kenh co the "co tin hieu" nhung
// van dang canh bao (vd nhip tim bat thuong), hoac "chua noi" (luon 0).
const SIGNAL_BIT_HR    = 0x20;
const SIGNAL_BIT_SPO2  = 0x40;
const SIGNAL_BIT_FLOW  = 0x80;
const SIGNAL_BIT_DROPS = 0x100;

const fzSmartIv = {
    heart_rate: {
        cluster: 'msTemperatureMeasurement',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.endpoint.ID !== 2 || msg.data.measuredValue === undefined) return;
            return {heart_rate: msg.data.measuredValue};
        },
    },
    spo2: {
        cluster: 'msRelativeHumidity',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.endpoint.ID !== 3 || msg.data.measuredValue === undefined) return;
            return {spo2: msg.data.measuredValue};
        },
    },
    flow: {
        cluster: 'msFlowMeasurement',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.endpoint.ID !== 4 || msg.data.measuredValue === undefined) return;
            return {flow: msg.data.measuredValue / 100};
        },
    },
    drop_rate: {
        cluster: 'msPressureMeasurement',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.endpoint.ID !== 5 || msg.data.measuredValue === undefined) return;
            return {drop_rate: msg.data.measuredValue / 100};
        },
    },
    alarm_bitmap: {
        cluster: 'msIlluminanceMeasurement',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.endpoint.ID !== 6 || msg.data.measuredValue === undefined) return;
            const bitmap = msg.data.measuredValue;
            return {
                alarm: (bitmap & 0x1F) !== 0,
                signal_lost: (bitmap & ALARM_BIT_MISSING) !== 0,
                spo2_low: (bitmap & ALARM_BIT_SPO2) !== 0,
                heart_rate_abnormal: (bitmap & ALARM_BIT_HR) !== 0,
                line_blocked: (bitmap & ALARM_BIT_FLOW) !== 0,
                ae_alarm: (bitmap & ALARM_BIT_AE) !== 0,
                hr_signal: (bitmap & SIGNAL_BIT_HR) !== 0,
                spo2_signal: (bitmap & SIGNAL_BIT_SPO2) !== 0,
                flow_signal: (bitmap & SIGNAL_BIT_FLOW) !== 0,
                drops_signal: (bitmap & SIGNAL_BIT_DROPS) !== 0,
            };
        },
    },
};

const definition = {
    // ModelID/manufacturerName qua cluster Basic khong doc duoc on tin cay khi
    // interview (device.modelID = undefined), nen nhan dien theo dung "van tay"
    // endpoint/cluster cua 6 endpoint firmware empty_2 khai bao trong zap thay
    // vi dua vao zigbeeModel string.
    fingerprint: [{
        endpoints: [
            {ID: 1, inputClusters: [0]},
            {ID: 2, inputClusters: [1026]},
            {ID: 3, inputClusters: [1029]},
            {ID: 4, inputClusters: [1028]},
            {ID: 5, inputClusters: [1027]},
            {ID: 6, inputClusters: [1024]},
        ],
    }],
    model: 'SmartIV-Sensor',
    vendor: 'ICTU',
    description: 'Smart IV drip monitor (AI Smart IV, EFR32xG26)',
    fromZigbee: [
        fzSmartIv.heart_rate,
        fzSmartIv.spo2,
        fzSmartIv.flow,
        fzSmartIv.drop_rate,
        fzSmartIv.alarm_bitmap,
    ],
    toZigbee: [],
    exposes: [
        e.numeric('heart_rate', ea.STATE).withUnit('bpm').withDescription('Nhip tim'),
        e.numeric('spo2', ea.STATE).withUnit('%').withDescription('Do bao hoa oxy'),
        e.numeric('flow', ea.STATE).withUnit('%').withDescription('Luu luong / muc dat'),
        e.numeric('drop_rate', ea.STATE).withUnit('%').withDescription('Giot/phut / muc dat'),
        e.binary('alarm', ea.STATE, true, false).withDescription('Co bao dong tong hop'),
        e.binary('signal_lost', ea.STATE, true, false).withDescription('Mat tin hieu'),
        e.binary('spo2_low', ea.STATE, true, false).withDescription('SpO2 thap'),
        e.binary('heart_rate_abnormal', ea.STATE, true, false).withDescription('Nhip tim bat thuong'),
        e.binary('line_blocked', ea.STATE, true, false).withDescription('Duong truyen tac/free-flow'),
        e.binary('ae_alarm', ea.STATE, true, false).withDescription('Autoencoder vuot nguong'),
        e.binary('hr_signal', ea.STATE, true, false).withDescription('Kenh nhip tim co tin hieu'),
        e.binary('spo2_signal', ea.STATE, true, false).withDescription('Kenh SpO2 co tin hieu'),
        e.binary('flow_signal', ea.STATE, true, false).withDescription('Kenh can/luu luong co tin hieu'),
        e.binary('drops_signal', ea.STATE, true, false).withDescription('Kenh giot co tin hieu'),
    ],
    configure: async (device, coordinatorEndpoint, logger) => {
        // Firmware da tu cau hinh reporting (interval/threshold) cuc bo ngay
        // luc vao mang (xem zb_configure_reporting() trong app.c) - o day chi
        // can tao BINDING de firmware biet gui report di dau. Khong goi
        // configureReporting() qua ZCL vi command nay hay bi timeout va lam
        // dut ca vong lap, khien cac endpoint sau khong duoc bind.
        const bindings = [
            {epId: 2, cluster: 'msTemperatureMeasurement'},
            {epId: 3, cluster: 'msRelativeHumidity'},
            {epId: 4, cluster: 'msFlowMeasurement'},
            {epId: 5, cluster: 'msPressureMeasurement'},
            {epId: 6, cluster: 'msIlluminanceMeasurement'},
        ];
        for (const b of bindings) {
            try {
                const ep = device.getEndpoint(b.epId);
                await reporting.bind(ep, coordinatorEndpoint, [b.cluster]);
            } catch (error) {
                logger.warning(`SmartIV-Sensor: bind endpoint ${b.epId} (${b.cluster}) failed: ${error.message}`);
            }
        }
    },
    meta: {},
};

module.exports = definition;
