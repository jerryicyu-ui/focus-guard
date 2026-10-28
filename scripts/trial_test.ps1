# 第三週逐次測試紀錄：每一次都重新操作一次，讀板子的 EVENT 事件自動判斷實際結果，存成作業要求的紀錄格式
# 用法：powershell -ExecutionPolicy Bypass -File scripts/trial_test.ps1 -Condition normal [-Phase 初測] [-Count 10] [-Port COM6]
#   -Condition normal     正常情境：拿起手機滑，應判定分心
#   -Condition occlusion  干擾情境：分心中用手遮住手機約 1 秒，不應結束、不應重複計數
#   -Condition nontarget  非目標情境：手拿書／杯子／遙控器，不應判定分心
# 每次結束可輸入 r 作廢重做（操作失誤時），作廢紀錄另外存檔並寫原因
param(
    [Parameter(Mandatory = $true)][ValidateSet('normal', 'occlusion', 'nontarget')][string]$Condition,
    [string]$Phase = '初測',
    [int]$Count = 10,
    [int]$StartAt = 1,
    [string]$Port = 'COM6'
)
$ErrorActionPreference = 'Stop'

$names = @{ normal = '正常情境'; occlusion = '干擾情境（遮擋）'; nontarget = '非目標情境（其他物品）' }
$items = @('書', '杯子', '遙控器')

$root = Split-Path -Parent $PSScriptRoot
$dataDir = Join-Path $root 'docs\test-data\week3'
New-Item -ItemType Directory -Force $dataDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$csvPath = Join-Path $dataDir "${Phase}_${Condition}_$stamp.csv"
$voidPath = Join-Path $dataDir "${Phase}_${Condition}_$stamp.void.csv"
$logPath = Join-Path $dataDir "${Phase}_${Condition}_$stamp.serial.log"
$utf8 = New-Object System.Text.UTF8Encoding($true)
$header = '階段,條件,測試編號,預期行為,實際行為,是否符合預期,備註,反應秒數,手機最高分,時間'
[IO.File]::WriteAllText($csvPath, $header + "`r`n", $utf8)

$serial = New-Object System.IO.Ports.SerialPort $Port, 115200
$serial.Open()
$script:buffer = ''

function Csv([string]$s) { '"' + ($s -replace '"', '""') + '"' }

# 讀取一段時間的序列埠，回傳這段時間內的事件（附電腦端經過秒數）與手機最高分
function Watch([double]$seconds, [Diagnostics.Stopwatch]$clock, [System.Collections.Generic.List[object]]$events, [ref]$maxPhone) {
    $end = $clock.Elapsed.TotalSeconds + $seconds
    while ($clock.Elapsed.TotalSeconds -lt $end) {
        $script:buffer += $serial.ReadExisting()
        $lines = $script:buffer -split "`n"
        $script:buffer = $lines[-1]
        foreach ($raw in $lines[0..($lines.Length - 2)]) {
            $line = $raw.Trim()
            if (-not $line) { continue }
            if ($line -match '^F,\d+,(\d+),(\d+),') {
                if ([int]$Matches[2] -gt $maxPhone.Value) { $maxPhone.Value = [int]$Matches[2] }
                # 逐幀的人／手機分數也存起來，失敗時才能分析原因
                [IO.File]::AppendAllText($logPath, ('{0,6:N2}s F person={1} phone={2}' -f $clock.Elapsed.TotalSeconds, $Matches[1], $Matches[2]) + "`r`n", $utf8)
                continue
            }
            if ($line -match '^EVENT,') {
                $events.Add([pscustomobject]@{ T = [math]::Round($clock.Elapsed.TotalSeconds, 2); Line = $line })
                [IO.File]::AppendAllText($logPath, ('{0,6:N2}s {1}' -f $clock.Elapsed.TotalSeconds, $line) + "`r`n", $utf8)
            }
        }
        Start-Sleep -Milliseconds 50
    }
}

function Say([string]$text, [string]$color = 'White') { Write-Host ("  ▶ " + $text) -ForegroundColor $color }

function Run-Trial([int]$n) {
    $events = New-Object System.Collections.Generic.List[object]
    $maxPhone = 0
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $serial.DiscardInBuffer(); $script:buffer = ''
    [IO.File]::AppendAllText($logPath, "--- 測試 $n ---`r`n", $utf8)

    switch ($Condition) {
        'normal' {
            Say '開始！拿起手機，用平常姿勢滑（約 8 秒）' 'Green'
            Watch 8 $clock $events ([ref]$maxPhone)
            Say '放下手機，放回原位' 'Yellow'
            $putDown = $clock.Elapsed.TotalSeconds
            Watch 5 $clock $events ([ref]$maxPhone)
            $starts = @($events | Where-Object { $_.Line -like 'EVENT,DISTRACT_START*' })
            $ends = @($events | Where-Object { $_.Line -like 'EVENT,DISTRACT_END*' -and $_.T -ge $putDown })
            $earlyEnds = @($events | Where-Object { $_.Line -like 'EVENT,DISTRACT_END*' -and $_.T -lt $putDown })
            $latency = if ($starts.Count) { $starts[0].T } else { $null }
            $ok = ($starts.Count -eq 1) -and ($latency -le 5) -and ($ends.Count -ge 1) -and ($earlyEnds.Count -eq 0)
            $actual = if ($starts.Count -eq 0) { '沒有判定分心' }
                      elseif ($earlyEnds.Count) { '拿起後 {0:N1} 秒判定分心，但滑手機途中分心就結束（手機偵測中斷超過 2 秒），分心次數 +{1}' -f $latency, $starts.Count }
                      else { '拿起後 {0:N1} 秒判定分心，分心次數 +{1}；放下後{2}' -f $latency, $starts.Count, $(if ($ends.Count) { '回到讀書中' } else { '5 秒內沒有結束分心' }) }
        }
        'occlusion' {
            Say '開始！拿起手機滑，等藍燈亮（約 5 秒）' 'Green'
            Watch 5 $clock $events ([ref]$maxPhone)
            Say '用手遮住手機（約 1 秒）' 'Yellow'
            $coverAt = $clock.Elapsed.TotalSeconds
            Watch 1 $clock $events ([ref]$maxPhone)
            Say '把手移開，繼續滑' 'Green'
            Watch 4 $clock $events ([ref]$maxPhone)
            Say '放下手機，放回原位' 'Yellow'
            $putDown = $clock.Elapsed.TotalSeconds
            Watch 5 $clock $events ([ref]$maxPhone)
            $starts = @($events | Where-Object { $_.Line -like 'EVENT,DISTRACT_START*' })
            $earlyEnds = @($events | Where-Object { $_.Line -like 'EVENT,DISTRACT_END*' -and $_.T -lt $putDown })
            $latency = if ($starts.Count) { $starts[0].T } else { $null }
            $distractedBeforeCover = $starts.Count -ge 1 -and $starts[0].T -le $coverAt
            $ok = $distractedBeforeCover -and ($starts.Count -eq 1) -and ($earlyEnds.Count -eq 0)
            $actual = if ($starts.Count -eq 0) { '沒有判定分心（遮擋前就未觸發）' }
                      elseif (-not $distractedBeforeCover) { '遮擋前尚未判定分心（{0:N1} 秒才觸發）' -f $latency }
                      elseif ($earlyEnds.Count -gt 0 -or $starts.Count -gt 1) { '遮擋時分心被中斷，分心次數 +{0}' -f $starts.Count }
                      else { '遮擋期間維持分心，分心次數只 +1' }
        }
        'nontarget' {
            $item = $items[($n - 1) % $items.Count]
            Say ("開始！手拿「{0}」，像平常使用一樣停留約 6 秒" -f $item) 'Green'
            Watch 6 $clock $events ([ref]$maxPhone)
            Say ("放下「{0}」" -f $item) 'Yellow'
            Watch 3 $clock $events ([ref]$maxPhone)
            $starts = @($events | Where-Object { $_.Line -like 'EVENT,DISTRACT_START*' })
            $pending = @($events | Where-Object { $_.Line -like 'EVENT,STATE,*,PHONE_PENDING' })
            $latency = $null
            $ok = $starts.Count -eq 0
            $actual = if ($starts.Count) { '誤判為分心（{0}）' -f $item }
                      elseif ($pending.Count) { '短暫出現「偵測到手機」但未判定分心（{0}）' -f $item }
                      else { '沒有判定分心（{0}）' -f $item }
        }
    }
    [pscustomobject]@{ Ok = $ok; Actual = $actual; Latency = $latency; MaxPhone = $maxPhone }
}

$expected = @{
    normal    = '拿起手機滑，5 秒內判定分心（藍燈），放下後回到讀書中，分心次數 +1'
    occlusion = '分心中用手遮住手機約 1 秒，維持分心不中斷，分心次數只 +1'
    nontarget = '手拿非手機物品約 6 秒，不判定分心'
}[$Condition]

try {
    Write-Host "`n=== $Phase｜$($names[$Condition])｜共 $Count 次 ===" -ForegroundColor Cyan
    Write-Host "預期行為：$expected"
    Write-Host '每次開始前：坐好、手機放在桌上固定位置，確認網頁顯示「讀書中」。' -ForegroundColor DarkGray
    $n = $StartAt
    while ($n -lt $StartAt + $Count) {
        Write-Host "`n[測試 $n]" -ForegroundColor Cyan
        Read-Host '準備好按 Enter' | Out-Null
        Write-Host '  倒數時先不要動，看到「開始」才動作' -ForegroundColor DarkGray
        foreach ($c in 3, 2, 1) { Write-Host "  $c..."; Start-Sleep -Seconds 1 }
        $r = Run-Trial $n
        $verdict = if ($r.Ok) { '是' } else { '否' }
        Write-Host ("  實際：{0}　→ 符合預期：{1}" -f $r.Actual, $verdict) -ForegroundColor $(if ($r.Ok) { 'Green' } else { 'Red' })
        $note = Read-Host '  備註（可留白；這次操作失誤要作廢重做請輸入 r）'
        if ($note -eq 'r') {
            $reason = Read-Host '  作廢原因（例如：太早放下手機）'
            if (-not (Test-Path $voidPath)) { [IO.File]::WriteAllText($voidPath, "測試編號,實際行為,作廢原因,時間`r`n", $utf8) }
            [IO.File]::AppendAllText($voidPath, (@($n, (Csv $r.Actual), (Csv $reason), (Get-Date -Format 'HH:mm:ss')) -join ',') + "`r`n", $utf8)
            Write-Host '  已作廢，重做這一次。' -ForegroundColor DarkYellow
            continue
        }
        $latencyText = if ($null -ne $r.Latency) { '{0:N1}' -f $r.Latency } else { '' }
        $row = @((Csv $Phase), (Csv $names[$Condition]), $n, (Csv $expected), (Csv $r.Actual), $verdict, (Csv $note), $latencyText, $r.MaxPhone, (Get-Date -Format 'HH:mm:ss')) -join ','
        [IO.File]::AppendAllText($csvPath, $row + "`r`n", $utf8)
        $n++
    }
}
finally {
    $serial.Close()
}

$rows = Import-Csv $csvPath -Encoding UTF8
$pass = @($rows | Where-Object { $_.是否符合預期 -eq '是' }).Count
Write-Host "`n=== 完成：$pass / $($rows.Count) 次符合預期 ===" -ForegroundColor Cyan
Write-Host "紀錄：$csvPath"
