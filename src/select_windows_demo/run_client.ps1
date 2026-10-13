param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('A', 'B')]
    [string]$Name,

    [ValidateRange(-1, 120)]
    [int]$DelaySeconds = -1
)

if ($DelaySeconds -eq -1) {
    if ($Name -eq 'A') { $DelaySeconds = 20 }
    else { $DelaySeconds = 0 }
}

$client = [System.Net.Sockets.TcpClient]::new()
try {
    $client.Connect('127.0.0.1', 19100)
    Write-Output "$Name connected; wait $DelaySeconds seconds before sending"
    if ($DelaySeconds -gt 0) { Start-Sleep -Seconds $DelaySeconds }
    $bytes = [System.Text.Encoding]::ASCII.GetBytes("hello from $Name")
    $stream = $client.GetStream()
    $stream.Write($bytes, 0, $bytes.Length)
    Write-Output "$Name submitted $($bytes.Length) bytes locally"
}
finally {
    $client.Dispose()
    Write-Output "$Name closed"
}
