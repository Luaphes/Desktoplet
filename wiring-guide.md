# ESP32-C3 SuperMini 接线指南

## 1. 电源

### ESP32 3V3 → 3.3V 总线（3根排针用跳线帽全短通）

| 总线排针编号 | 连接目标 |
|-------------|---------|
| ① | ESP32 3V3 排针 |
| ② | OLED VCC |
| ③ | INMP441 VDD |

### ESP32 GND → GND 总线（5根排针用跳线帽全短通）

| 总线排针编号 | 连接目标 |
|-------------|---------|
| ① | ESP32 GND 排针 + TP4056 OUT- |
| ② | OLED GND |
| ③ | INMP441 GND |
| ④ | INMP441 L/R |
| ⑤ | 按键另一脚 |

### ESP32 5V → TP4056 OUT+

一根杜邦线从 TP4056 OUT+ 排针插到 ESP32 5V 排针。

调试/刷固件时：拔掉这根线，改插 USB-C 到 ESP32。

## 2. 信号线

| ESP32 引脚 | 杜邦线→ | 元件引脚 | 备注 |
|------------|--------|---------|------|
| GPIO0 | → | 按键一脚 | 另一脚→GND总线⑤ |
| GPIO2 | → | INMP441 WS | I2S帧同步 |
| GPIO3 | → | INMP441 SCK | I2S时钟 |
| GPIO4 | → | INMP441 SD | I2S数据 |
| GPIO8 | → | OLED SDA | I2C数据 |
| GPIO9 | → | OLED SCL | I2C时钟 |

## 3. TP4056（已由手机店焊好）

| TP4056 焊盘 | 焊了什么 |
|-------------|---------|
| BAT+ | 电池红线 |
| BAT- | 电池黑线 |
| OUT+ | 排针→杜邦线→ESP32 5V |
| OUT- | 排针→杜邦线→GND总线① |

## 4. 按键接法

按键两只脚。剪掉母杜邦线头，剥出铜线缠绕按键脚:
- 一脚缠线 → GPIO0 排针
- 另一脚缠线 → GND总线⑤

## 5. GND 总线图（ASCII，尽量不折行）

```
①──跳线帽──②──跳线帽──③──跳线帽──④──跳线帽──⑤
│          │          │          │          │
▼          ▼          ▼          ▼          ▼
ESP32    TP4056    OLED     MIC      MIC      按键
GND      OUT-      GND      GND      L/R      GND
```

## 6. 3.3V 总线图

```
①──跳线帽──②──跳线帽──③
│          │          │
▼          ▼          ▼
ESP32     OLED      INMP441
3V3       VCC       VDD
```

## 7. 上电启动顺序

1. 全部插好后，先插 TP4056 的 USB 充电线（或接上电池）
2. ESP32 红灯常亮=通电正常
3. 第一次需要用 USB-C 线连 Mac 烧固件，之后全走 OTA

## 8. 接线核对清单

完成后逐项打勾：

- [ ] TP4056 OUT+ → ESP32 5V（调试时断开）
- [ ] TP4056 OUT- → GND总线①
- [ ] ESP32 GND → GND总线①
- [ ] ESP32 3V3 → 3.3V总线①
- [ ] OLED VCC → 3.3V总线②
- [ ] OLED GND → GND总线②
- [ ] OLED SDA → GPIO8
- [ ] OLED SCL → GPIO9
- [ ] INMP441 VDD → 3.3V总线③
- [ ] INMP441 GND → GND总线③
- [ ] INMP441 L/R → GND总线④
- [ ] INMP441 SD → GPIO4
- [ ] INMP441 SCK → GPIO3
- [ ] INMP441 WS → GPIO2
- [ ] 按键脚1 → GPIO0
- [ ] 按键脚2 → GND总线⑤
