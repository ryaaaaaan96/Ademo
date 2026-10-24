param([Parameter(Mandatory=$true)][string]$BodyHex)
# 独立软件向量计算，不读取或调用产品UPP/Modbus实现。
$crc = 65535
for ($offset = 0; $offset -lt $BodyHex.Length; $offset += 2) {
    $value = [Convert]::ToByte($BodyHex.Substring($offset, 2), 16)
    $crc = $crc -bxor $value
    for ($bit = 0; $bit -lt 8; $bit++) {
        if ($crc -band 1) { $crc = ($crc -shr 1) -bxor 40961 }
        else { $crc = $crc -shr 1 }
    }
}
$BodyHex + ('{0:x2}{1:x2}' -f ($crc -band 255), ($crc -shr 8))
