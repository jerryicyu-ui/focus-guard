# Minimal RTSP client: OPTIONS -> DESCRIBE -> SETUP (RTP over TCP) -> PLAY, then count video bytes for a few seconds.
# TCP interleaved is used so the Windows firewall (Public profile blocks inbound UDP) cannot hide packets.
# Used to tell apart "board does not send video" from "VLC problem". ASCII-only for Windows PowerShell 5.1.
param([string]$Ip = '172.20.10.13', [int]$Seconds = 6)
$ErrorActionPreference = 'Stop'
$url = "rtsp://${Ip}:554"
$tcp = New-Object System.Net.Sockets.TcpClient
$tcp.ReceiveTimeout = 8000
$tcp.Connect($Ip, 554)
$stream = $tcp.GetStream()

function Send-Rtsp([string]$text) {
    $bytes = [Text.Encoding]::ASCII.GetBytes($text)
    $stream.Write($bytes, 0, $bytes.Length)
    $buf = New-Object byte[] 4096
    $n = $stream.Read($buf, 0, $buf.Length)
    $reply = [Text.Encoding]::ASCII.GetString($buf, 0, $n)
    Write-Host ("<<< " + ($reply -split "`r`n")[0])
    return $reply
}

try {
    Send-Rtsp "OPTIONS $url RTSP/1.0`r`nCSeq: 1`r`n`r`n" | Out-Null
    Send-Rtsp "DESCRIBE $url RTSP/1.0`r`nCSeq: 2`r`nAccept: application/sdp`r`n`r`n" | Out-Null
    $setup = Send-Rtsp "SETUP $url/streamid=0 RTSP/1.0`r`nCSeq: 3`r`nTransport: RTP/AVP/TCP;unicast;interleaved=0-1`r`n`r`n"
    $session = ([regex]::Match($setup, 'Session:\s*([^;\r\n]+)')).Groups[1].Value
    Write-Host "session=$session"
    Send-Rtsp "PLAY $url RTSP/1.0`r`nCSeq: 4`r`nSession: $session`r`nRange: npt=0.000-`r`n`r`n" | Out-Null

    $tcp.ReceiveTimeout = 1000
    $bytesTotal = 0
    $buf = New-Object byte[] 65536
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        try { $bytesTotal += $stream.Read($buf, 0, $buf.Length) } catch {}
    }
    Write-Host "video bytes in ${Seconds}s: $bytesTotal"
    $bytes = [Text.Encoding]::ASCII.GetBytes("TEARDOWN $url RTSP/1.0`r`nCSeq: 5`r`nSession: $session`r`n`r`n")
    $stream.Write($bytes, 0, $bytes.Length)
}
finally {
    $tcp.Close()
}
