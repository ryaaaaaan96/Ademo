# Modbus 实现路径核验

核验日期：2026-10-22。适用 [02：首次 RTU 保持寄存器读取](issues/02-rtu-first-register-read.md)。本页记录落地前的路径决定，后续功能不因此视为已经实现。

## 选择

采用项目自有的无 I/O Modbus 协议内核，由 Modbus 插件管理事务、RTU 成帧与功能码语义；宿主提供统一通道、时钟及原始记录，实际端点复用 01 的 QSerialPort 插件。只在 02 落地功能码 `03` 主站单次读取，其余主从行为随对应 issue 交付。

这是针对已确认架构的设计结论：协议 PDU 与承载分离，后续能复用数据语义，不需要协议插件自行打开串口。以规范向量、可控传输与时钟验证，再由独立对端和真实串口补硬件验收；自行实现也意味着项目承担成帧、校验、异常和互通测试责任。

| 路径 | 外部 I/O 与主从适配核验 | 本次决定 |
| --- | --- | --- |
| 自有协议内核 + 统一通道 | 输入字节及观测时间，输出发送意图；主站事务、从站处理与未来传输映射均留在协议模块，记录先于解析 | 采用；当前仅实现 03 主站读取 |
| Qt QModbus RTU 完整栈 | Qt 提供 RTU client/server；client 的公开接口为读写请求、超时与重试，RTU 类负责串口 open/close。未发现可将既有 RTU client 的 I/O 整体替换为任意字节回调的公开注入入口 | 不直接使用整栈，避免绕开公共通道；自行实现 QModbusDevice 子类不等于直接复用其 RTU 后端 |
| libmodbus | 官方有 RTU 主从及八功能码 API；RTU context 绑定系统端口并 connect，`modbus_set_socket` 提供描述符设置，而非宿主异步字节/时钟回调契约 | 不引入；适配自定义后端及当前 LLVM-MinGW 构建需额外核验，未宣称其不可扩展 |

Qt 依据：[QModbusRtuSerialClient](https://doc.qt.io/qt-6/qmodbusrtuserialclient.html)、[QModbusRtuSerialServer](https://doc.qt.io/qt-6/qmodbusrtuserialserver.html)、[QModbusDevice](https://doc.qt.io/qt-6/qmodbusdevice.html)。另读取本机 `C:/Qt/6.12.0/llvm-mingw_64/include/QtSerialBus/qmodbusrtuserialclient.h`、`qmodbusclient.h` 和 `QtSerialPort/qserialport.h`，结论与公开接口一致。

libmodbus 依据：[RTU context](https://libmodbus.org/reference/modbus_new_rtu/)、[set_socket](https://libmodbus.org/reference/modbus_set_socket/)、[官方公共头文件](https://github.com/stephane/libmodbus/blob/master/src/modbus.h)。本次未下载、编译、链接或复制该库。

## 主从及八功能码的后续适配依据

下表是规范规定的扩展路径，不是当前支持列表。主站构建请求并核验响应，从站解码请求、验证数据范围、访问对应数据区后构建正常或异常响应；共同复用 PDU 大端字段、RTU 地址/CRC 与成帧规则。自有实现没有外部库所限定的功能码集合。

| 功能码（十六进制） | 主站请求 / 从站动作 | 规范章节 |
| --- | --- | --- |
| 01 / 02 | 读取线圈 / 离散输入，以位打包返回 | §6.1 / §6.2 |
| 03 / 04 | 读取保持寄存器 / 输入寄存器，以 16 位值返回 | §6.3 / §6.4 |
| 05 / 06 | 写单线圈 / 单保持寄存器，校验后返回写入参数 | §6.5 / §6.6 |
| 0F / 10 | 批量写线圈 / 保持寄存器，校验数量和值后返回地址及数量 | §6.11 / §6.12 |

依据：[Modbus 应用协议 V1.1b3](https://www.modbus.org/file/secure/modbusprotocolspecification.pdf)，§4.2–4.4（第 5–7 页）、§5.1（第 11 页）及表中章节。数据模型在协议插件中扩展；主站观测值和从站模拟数据保持分离。后续从站/八功能码测试不能用本项 03 主站测试替代。

## 02 必须落实的边界

- `03` 使用从 0 开始的 PDU 起始地址 `0..65535`，数量 `1..125`，范围末端不得越过 65535；响应每个寄存器高字节在前，字节计数必须为数量的两倍。异常功能码为 `83`；异常与超时分别报告。依据：[应用协议](https://www.modbus.org/file/secure/modbusprotocolspecification.pdf) §6.3，第 15–16 页；通用异常见 §7，第 47–49 页。
- 单播从站地址 `1..247`；地址 0 是仅用于写命令的广播，不接受广播读取。同一串行总线主站一次发起一个事务。依据：[串行规范 V1.02](https://www.modbus.org/file/secure/modbusoverserial.pdf) §2.1–2.2，第 7–8 页。
- RTU 帧最多 256 字节。CRC 起始值 `FFFF`，右移多项式 `A001`，传输时 CRC 低字节在前。依据：[串行规范](https://www.modbus.org/file/secure/modbusoverserial.pdf) §2.5.1，第 13 页；§2.5.1.2，第 14–15 页；附录 B，第 39–40 页。实现按算法独立编写，不复制规范中的示例代码或查表。
- RTU 字符为 11 位；默认偶校验，无校验时必须配置两个停止位。`<=19200` 波特率按 1.5 / 3.5 字符时间处理帧内 / 帧间静默；`>19200` 按规范建议使用 750 / 1750 微秒。初始化到空闲同样先等待 t3.5。依据：[串行规范](https://www.modbus.org/file/secure/modbusoverserial.pdf) §2.5.1–2.5.1.1，第 12–14 页。
- 不能用一次 `readyRead` 当完整 RTU 帧；帧内间隙超过 t1.5 时判不完整，等待 t3.5 确认边界。协议按可注入单调时钟执行规则，原始块先记录后解析。QSerialPort 的公开字节块接口不提供本项所需的逐字节硬件时间戳，因此可控时钟证明规则，不能证明 Windows / USB 聚合接收的物理时序精度。硬件精度与实际互通保留为明确的验收限制。
- 提交发送、QSerialPort 本地写入进展与设备应答是不同事实。串口写入可能被缓冲或仅接纳部分字节，剩余部分须按后端进展继续提交；不能把本地完成显示成读取成功。依据：[QSerialPort](https://doc.qt.io/qt-6/qserialport.html) 的 `setWriteBufferSize` / `bytesWritten` / `waitForBytesWritten` 文档。

## 分发要求

本次无新增 Modbus 第三方库二进制或源码依赖，不引入 libmodbus 的 LGPL 义务；若未来改用 libmodbus，应按具体引入版本重新核验许可，官方当前标明 [LGPL-2.1-or-later](https://github.com/stephane/libmodbus/blob/master/README.md)，不能把候选评估视为已获额外授权。

继续动态链接已有 Qt 模块。[Qt Serial Port 官方许可页](https://doc.qt.io/qt-6/qtserialport-index.html#licenses) 列出商业许可、LGPLv3 或 GPLv2 路径；Qt Core/Widgets 等依赖也须按实际选用路径分发相应通知、许可证及所需材料。动态链接本身不能证明全部义务已经满足，最终 Windows 交付由 19 记录和验证。

仓库当前未发现项目 LICENSE 文件。自有实现不意味着自动授予项目对外分发许可，本项不替用户新增许可证。Modbus 规范仅作为规则依据引用，不随产品复制全文；本页事实核验与实现路径选择不构成完整发布许可审计。
