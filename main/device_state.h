#ifndef _DEVICE_STATE_H_
#define _DEVICE_STATE_H_

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError,
    // Intercom states (Full Duplex)
    kDeviceStateIntercomCalling,   // Đang gọi, chờ kết nối
    kDeviceStateIntercomActive,    // Đang trong cuộc gọi (full duplex)
    kDeviceStateIntercomIncoming   // Có cuộc gọi đến
};

#endif // _DEVICE_STATE_H_ 