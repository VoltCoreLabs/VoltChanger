$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $root 'vendor/protos'
New-Item -ItemType Directory -Force $destination | Out-Null
# Pin the protocol snapshot; generated sources must be regenerated together.
$lock = Join-Path $destination 'revision.txt'
if (Test-Path $lock) { $revision = (Get-Content $lock -Raw).Trim() }
else {
    $revision = (Invoke-RestMethod 'https://api.github.com/repos/SteamDatabase/Protobufs/commits/master').sha
    Set-Content -LiteralPath $lock -Value $revision -Encoding ascii
}
$pending = [System.Collections.Generic.Queue[string]]::new()
@('base_gcmessages.proto','dota_usermessages.proto','econ_gcmessages.proto','gcsdk_gcmessages.proto','netmessages.proto','steammessages.proto','usermessages.proto') | ForEach-Object { $pending.Enqueue($_) }
$seen = [System.Collections.Generic.HashSet[string]]::new()
while ($pending.Count -gt 0) {
    $name = $pending.Dequeue()
    if ($name.StartsWith('google/') -or -not $seen.Add($name)) { continue }
    $file = Join-Path $destination $name
    if (-not (Test-Path -LiteralPath $file)) {
        Invoke-WebRequest "https://raw.githubusercontent.com/SteamDatabase/Protobufs/$revision/dota2/$name" -OutFile $file -UseBasicParsing
    }
    $content = Get-Content -LiteralPath $file -Raw
    foreach ($match in [regex]::Matches($content, 'import\s+(?:(?:public|weak)\s+)?"([^"]+)"')) { $pending.Enqueue($match.Groups[1].Value) }
}
Write-Host "Protocols: $($seen.Count) files at $revision"
$prepared = Join-Path $root 'build/protos'
New-Item -ItemType Directory -Force $prepared | Out-Null
foreach ($name in $seen) {
    # Valve's descriptor has built-in codegen-only options absent from stock protoc.
    # Strip only these annotations; preserve all message fields and wire defaults.
    $content = Get-Content -LiteralPath (Join-Path $destination $name) -Raw
    $content = [regex]::Replace($content, '(?m)^\s*option additional_includes\s*=\s*"[^"]*";\s*$', '')
    $content = [regex]::Replace($content, '\[([^\]\r\n]*)\]', {
        param($match)
        $options = @($match.Groups[1].Value -split ',' | Where-Object { $_ -notmatch '^\s*(boxed_type|synthetic_default)\s*=' })
        if ($options.Count) { '[' + ($options -join ',') + ']' } else { '' }
    })
    $content = 'syntax = "proto2";' + "`n" + $content
    $output = Join-Path $prepared $name
    if (-not (Test-Path $output) -or (Get-Content $output -Raw) -cne $content) {
        [System.IO.File]::WriteAllText($output, $content, [System.Text.UTF8Encoding]::new($false))
    }
}
