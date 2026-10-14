# 鏡頭擺放位置測試：讀取板子每一幀的 F,時間,person,phone,狀態，依步驟統計
# 用法：powershell -ExecutionPolicy Bypass -File scripts/placement_test.ps1 -Position B側後方 -Port COM6
#       只重跑某些步驟：加上 -Steps 3（多個用逗號：-Steps 2,3）
param(
    [Parameter(Mandatory = $true)][string]$Position,
    [string]$Port = 'COM6',
    [int[]]$Steps = @(1, 2, 3, 4)
)
$ErrorActionPreference = 'Stop'

$stepTable = @(
    @{ Name = '1_無人';       Seconds = 10; Hint = '離開座位，鏡頭前不要有人' },
    @{ Name = '2_讀書無手機'; Seconds = 15; Hint = '坐好看書或寫字，手機收起來' },
    @{ Name = '3_滑手機';     Seconds = 20; Hint = '用平常的姿勢滑手機（不要刻意對鏡頭）' },
    @{ Name = '4_手機放桌上'; Seconds = 10; Hint = '手機螢幕朝上放在桌面，人坐著不碰它' }
)

$stepTable = @($stepTable | Where-Object { $Steps -contains [int]($_.Name.Substring(0, 1)) })

$root = Split-Path -Parent $PSScriptRoot
$dataDir = Join-Path $root 'docs\test-data'
New-Item -ItemType Directory -Force $dataDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rawCsv = Join-Path $dataDir "placement_${Position}_$stamp.csv"
$serialLog = Join-Path $dataDir "placement_${Position}_$stamp.serial.log"
$summaryCsv = Join-Path $dataDir 'placement_summary.csv'
$utf8 = New-Object System.Text.UTF8Encoding($true)

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200
$serial.ReadTimeout = 300
$serial.Open()

$rows = New-Object System.Collections.Generic.List[string]
$rows.Add('position,step,board_ms,person,phone,state')
$summary = @()

try {
    Write-Host "`n=== 擺放位置：$Position ===" -ForegroundColor Cyan
    Write-Host '請先確認板子已經在執行（序列埠有輸出），並關閉其他序列埠視窗。'
    foreach ($step in $stepTable) {
        Write-Host "`n[$($step.Name)] $($step.Hint)（$($step.Seconds) 秒）" -ForegroundColor Yellow
        Read-Host '擺好姿勢後按 Enter 開始' | Out-Null
        foreach ($n in 3, 2, 1) { Write-Host "  $n..."; Start-Sleep -Seconds 1 }
        Write-Host '  記錄中，保持動作' -ForegroundColor Green

        $serial.DiscardInBuffer()
        $buffer = ''
        $frames = New-Object System.Collections.Generic.List[object]
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $step.Seconds) {
            $buffer += $serial.ReadExisting()
            $lines = $buffer -split "`n"
            $buffer = $lines[-1]
            foreach ($line in $lines[0..($lines.Length - 2)]) {
                if ($line.Trim() -and $line.Trim() -notmatch '^F,') {
                    # 其他訊息（Wi-Fi、RTSP 連線等）另外存起來，畫面斷掉時可以查原因
                    [IO.File]::AppendAllText($serialLog, "[$($step.Name)] $line`n", $utf8)
                }
                $f = $line.Trim() -split ','
                if ($f.Length -eq 5 -and $f[0] -eq 'F') {
                    $frames.Add([pscustomobject]@{ Ms = $f[1]; Person = [int]$f[2]; Phone = [int]$f[3]; State = $f[4] })
                    $rows.Add("$Position,$($step.Name),$($f[1]),$($f[2]),$($f[3]),$($f[4])")
                }
            }
            Start-Sleep -Milliseconds 100
        }

        $n = [Math]::Max($frames.Count, 1)
        $result = [pscustomobject]@{
            位置         = $Position
            步驟         = $step.Name
            幀數         = $frames.Count
            '人>=50 %'   = [Math]::Round(100 * @($frames | Where-Object { $_.Person -ge 50 }).Count / $n)
            '手機>=50 %' = [Math]::Round(100 * @($frames | Where-Object { $_.Phone -ge 50 }).Count / $n)
            '手機30-49 %' = [Math]::Round(100 * @($frames | Where-Object { $_.Phone -ge 30 -and $_.Phone -lt 50 }).Count / $n)
            人最高分     = ($frames | Measure-Object Person -Maximum).Maximum
            手機最高分   = ($frames | Measure-Object Phone -Maximum).Maximum
        }
        $summary += $result
        Write-Host ("  完成：{0} 幀，人≥50 {1}%，手機≥50 {2}%，手機 30-49 {3}%" -f $result.幀數, $result.'人>=50 %', $result.'手機>=50 %', $result.'手機30-49 %')
    }
}
finally {
    $serial.Close()
}

[IO.File]::WriteAllLines($rawCsv, $rows, $utf8)
$summary | Export-Csv -Path $summaryCsv -Append -NoTypeInformation -Encoding UTF8
Write-Host "`n=== $Position 結果 ===" -ForegroundColor Cyan
$summary | Format-Table -AutoSize
Write-Host "逐幀資料：$rawCsv"
Write-Host "彙總（所有位置累加）：$summaryCsv"
