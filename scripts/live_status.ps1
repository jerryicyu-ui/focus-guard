# 錄影用即時狀態顯示：讀板子序列埠，只顯示狀態改變與每秒狀態，並用顏色區分
# 用法：powershell -ExecutionPolicy Bypass -File scripts/live_status.ps1 [-Port COM6]
# 結束：按 Ctrl+C
param([string]$Port = 'COM6')
$ErrorActionPreference = 'Stop'

# 第一週（focus_check）與第二週（focus_guard）的狀態都支援
$colors = @{ 'NO_TARGET' = 'DarkGray'; 'PERSON' = 'Green'; 'UNSURE' = 'Yellow'; 'PHONE' = 'Red';
             'AWAY' = 'DarkGray'; 'STUDYING' = 'Green'; 'PHONE_PENDING' = 'Yellow'; 'DISTRACTED' = 'Red' }
$labels = @{ 'NO_TARGET' = '沒有目標'; 'PERSON' = '有人'; 'UNSURE' = '不確定'; 'PHONE' = '偵測到手機';
             'AWAY' = '離座'; 'STUDYING' = '讀書中'; 'PHONE_PENDING' = '手機確認中'; 'DISTRACTED' = '分心中' }

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200
$serial.Open()
Clear-Host
Write-Host '=== 讀書專注小幫手｜AMB82-MINI 板上即時辨識結果（序列埠）===' -ForegroundColor Cyan
Write-Host '第一週：綠燈 = 有人，藍燈 = 手機 ≥ 50　｜　第二週：綠燈 = 讀書中，藍燈 = 分心中（手機連續 2 秒）' -ForegroundColor Cyan
Write-Host ''

$buffer = ''
try {
    while ($true) {
        $buffer += $serial.ReadExisting()
        $lines = $buffer -split "`n"
        $buffer = $lines[-1]
        foreach ($raw in $lines[0..($lines.Length - 2)]) {
            $line = $raw.Trim()
            # 狀態改變：[12345 ms] PERSON -> PHONE (person=82 phone=61)
            $change = [regex]::Match($line, '^\[(\d+) ms\] (\w+) -> (\w+) \(person=(\d+) phone=(\d+)\)')
            if ($change.Success) {
                $to = $change.Groups[3].Value
                $text = '{0,8:N1}s  狀態改變 → {1,-6}  人={2,3}  手機={3,3}' -f ([int]$change.Groups[1].Value / 1000), $labels[$to], $change.Groups[4].Value, $change.Groups[5].Value
                Write-Host $text -ForegroundColor $colors[$to]
                continue
            }
            # 第二週事件：EVENT,DISTRACT_START,時間,次數 / EVENT,DISTRACT_END,時間,次數,持續毫秒
            $start = [regex]::Match($line, '^EVENT,DISTRACT_START,(\d+),(\d+)')
            if ($start.Success) {
                Write-Host ('{0,8:N1}s  ★ 第 {1} 次分心開始' -f ([int]$start.Groups[1].Value / 1000), $start.Groups[2].Value) -ForegroundColor Red
                continue
            }
            $end = [regex]::Match($line, '^EVENT,DISTRACT_END,(\d+),(\d+),(\d+)')
            if ($end.Success) {
                Write-Host ('{0,8:N1}s  ☆ 第 {1} 次分心結束，持續 {2:N1} 秒' -f ([int]$end.Groups[1].Value / 1000), $end.Groups[2].Value, ([int]$end.Groups[3].Value / 1000)) -ForegroundColor Magenta
                continue
            }
            if ($line -match '^EVENT,RESET$') {
                Write-Host '          統計已歸零' -ForegroundColor Cyan
                continue
            }
            # 每秒狀態：[12345 ms] state=PERSON person=82 phone=0 frames/s=6
            $beat = [regex]::Match($line, '^\[(\d+) ms\] state=(\w+) person=(\d+) phone=(\d+) frames/s=(\d+)(?: count=(\d+) study=(\d+)s distract=(\d+)s)?')
            if ($beat.Success) {
                $state = $beat.Groups[2].Value
                $text = '{0,8:N1}s  {1,-6}  人={2,3}  手機={3,3}  （每秒 {4} 張）' -f ([int]$beat.Groups[1].Value / 1000), $labels[$state], $beat.Groups[3].Value, $beat.Groups[4].Value, $beat.Groups[5].Value
                if ($beat.Groups[6].Success) {
                    $text += '  分心 {0} 次｜讀書 {1} 秒｜分心 {2} 秒' -f $beat.Groups[6].Value, $beat.Groups[7].Value, $beat.Groups[8].Value
                }
                $color = if ($colors.ContainsKey($state)) { $colors[$state] } else { 'DarkCyan' }
                Write-Host $text -ForegroundColor $color
            }
        }
        Start-Sleep -Milliseconds 100
    }
}
finally {
    $serial.Close()
}
