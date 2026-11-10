# Generates SampleProject/Assets/Models/DualityBot.gltf (+ DualityBot_Checker.png): a small
# procedurally built, CC0 sample model -- a static textured body and a 4-bone skinned arm with two
# animation takes ("Wave", "Spin"). Used to verify the FBX/glTF import pipeline end to end.
param([string]$OutDir)
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null

$buffer = New-Object System.Collections.Generic.List[byte]
$views = @(); $accessors = @()
function Align4 { while ($buffer.Count % 4) { $buffer.Add(0) } }
function PushFloats([double[]]$values) { Align4; $o = $buffer.Count; foreach ($v in $values) { $buffer.AddRange([BitConverter]::GetBytes([single]$v)) }; return $o }
function PushU16([int[]]$values) { Align4; $o = $buffer.Count; foreach ($v in $values) { $buffer.AddRange([BitConverter]::GetBytes([uint16]$v)) }; return $o }
function PushU8([int[]]$values) { Align4; $o = $buffer.Count; foreach ($v in $values) { $buffer.Add([byte]$v) }; return $o }
function AddAccessor($offset, $length, $componentType, $count, $type, $extra) {
    $script:views += ,([ordered]@{ buffer = 0; byteOffset = $offset; byteLength = $length })
    $a = [ordered]@{ bufferView = $script:views.Count - 1; componentType = $componentType; count = $count; type = $type }
    if ($extra) { foreach ($k in $extra.Keys) { $a[$k] = $extra[$k] } }
    $script:accessors += ,$a
    return $script:accessors.Count - 1
}

# Box with flat normals: 24 vertices, 36 indices.
function Box($cx, $cy, $cz, $sx, $sy, $sz, $pos, $nrm, $uv, $idx) {
    $faces = @(
        @(@(1,0,0), @(0,1,0), @(0,0,1)), @(@(-1,0,0), @(0,1,0), @(0,0,-1)),
        @(@(0,1,0), @(0,0,1), @(1,0,0)), @(@(0,-1,0), @(0,0,1), @(-1,0,0)),
        @(@(0,0,1), @(0,1,0), @(-1,0,0)), @(@(0,0,-1), @(0,1,0), @(1,0,0)))
    foreach ($f in $faces) {
        $n = $f[0]; $u = $f[2]; $v = $f[1]
        $base = $pos.Count / 3
        foreach ($c in @(@(-1,-1), @(1,-1), @(1,1), @(-1,1))) {
            $x = $cx + ($n[0] * $sx + $u[0] * $c[0] * $sx + $v[0] * $c[1] * $sx) / 2
            $y = $cy + ($n[1] * $sy + $u[1] * $c[0] * $sy + $v[1] * $c[1] * $sy) / 2
            $z = $cz + ($n[2] * $sz + $u[2] * $c[0] * $sz + $v[2] * $c[1] * $sz) / 2
            $pos.AddRange([double[]]@($x, $y, $z)); $nrm.AddRange([double[]]@($n[0], $n[1], $n[2]))
            $uv.AddRange([double[]]@((($c[0] + 1) / 2), (($c[1] + 1) / 2)))
        }
        $idx.AddRange([int[]]@($base, ($base + 1), ($base + 2), $base, ($base + 2), ($base + 3)))
    }
}

# ---- Body (static) ----
$bp = New-Object System.Collections.Generic.List[double]; $bn = New-Object System.Collections.Generic.List[double]
$bu = New-Object System.Collections.Generic.List[double]; $bi = New-Object System.Collections.Generic.List[int]
Box 0 0.3 0 1.0 0.6 0.6 $bp $bn $bu $bi
Box 0 0.85 0 0.5 0.5 0.5 $bp $bn $bu $bi   # head
$bodyPos = AddAccessor (PushFloats $bp.ToArray()) ($bp.Count * 4) 5126 ($bp.Count / 3) "VEC3" ([ordered]@{ min = @(-0.5, 0, -0.3); max = @(0.5, 1.1, 0.3) })
$bodyNrm = AddAccessor (PushFloats $bn.ToArray()) ($bn.Count * 4) 5126 ($bn.Count / 3) "VEC3" $null
$bodyUv = AddAccessor (PushFloats $bu.ToArray()) ($bu.Count * 4) 5126 ($bu.Count / 2) "VEC2" $null
$bodyIdx = AddAccessor (PushU16 $bi.ToArray()) ($bi.Count * 2) 5123 $bi.Count "SCALAR" $null

# ---- Arm (skinned to Joint0..3) ----
$ap = New-Object System.Collections.Generic.List[double]; $an = New-Object System.Collections.Generic.List[double]
$au = New-Object System.Collections.Generic.List[double]; $ai = New-Object System.Collections.Generic.List[int]
$joints = New-Object System.Collections.Generic.List[int]; $weights = New-Object System.Collections.Generic.List[double]
for ($k = 0; $k -lt 4; $k++) {
    $before = $ap.Count / 3
    Box 0.62 (0.25 + 0.4 * $k) 0 0.18 0.4 0.18 $ap $an $au $ai
    for ($v = $before; $v -lt $ap.Count / 3; $v++) { $joints.AddRange([int[]]@($k, 0, 0, 0)); $weights.AddRange([double[]]@(1, 0, 0, 0)) }
}
$armPos = AddAccessor (PushFloats $ap.ToArray()) ($ap.Count * 4) 5126 ($ap.Count / 3) "VEC3" ([ordered]@{ min = @(0.53, 0.05, -0.09); max = @(0.71, 1.65, 0.09) })
$armNrm = AddAccessor (PushFloats $an.ToArray()) ($an.Count * 4) 5126 ($an.Count / 3) "VEC3" $null
$armUv = AddAccessor (PushFloats $au.ToArray()) ($au.Count * 4) 5126 ($au.Count / 2) "VEC2" $null
$armJ = AddAccessor (PushU8 $joints.ToArray()) $joints.Count 5121 ($joints.Count / 4) "VEC4" $null
$armW = AddAccessor (PushFloats $weights.ToArray()) ($weights.Count * 4) 5126 ($weights.Count / 4) "VEC4" $null
$armIdx = AddAccessor (PushU16 $ai.ToArray()) ($ai.Count * 2) 5123 $ai.Count "SCALAR" $null
$ibm = @(); for ($k = 0; $k -lt 4; $k++) { $ibm += @(1,0,0,0, 0,1,0,0, 0,0,1,0, -0.62, (-(0.05 + 0.4 * $k)), 0, 1) }
$ibmAcc = AddAccessor (PushFloats $ibm) ($ibm.Count * 4) 5126 4 "MAT4" $null

# ---- Animations ----
$times = @(); $n = 16; for ($i = 0; $i -le $n; $i++) { $times += 2.0 * $i / $n }
$timeAcc = AddAccessor (PushFloats $times) ($times.Count * 4) 5126 $times.Count "SCALAR" ([ordered]@{ min = @(0.0); max = @(2.0) })
$samplers = @(); $channels = @()
for ($k = 1; $k -lt 4; $k++) {
    $q = @()
    foreach ($t in $times) { $a = 0.45 * [Math]::Sin(2 * [Math]::PI * $t / 2.0 + $k * 0.6); $q += @(0, 0, [Math]::Sin($a / 2), [Math]::Cos($a / 2)) }
    $acc = AddAccessor (PushFloats $q) ($q.Count * 4) 5126 $times.Count "VEC4" $null
    $samplers += ,([ordered]@{ input = $timeAcc; output = $acc; interpolation = "LINEAR" })
    $channels += ,([ordered]@{ sampler = $samplers.Count - 1; target = [ordered]@{ node = 2 + $k; path = "rotation" } })
}
$spinQ = @(); foreach ($t in $times) { $a = [Math]::PI * $t; $spinQ += @(0, [Math]::Sin($a / 2), 0, [Math]::Cos($a / 2)) }
$spinAcc = AddAccessor (PushFloats $spinQ) ($spinQ.Count * 4) 5126 $times.Count "VEC4" $null

# ---- Checker texture (external file next to the model, 100x60: NPOT on purpose) ----
$bmp = New-Object System.Drawing.Bitmap 100, 60
for ($y = 0; $y -lt 60; $y++) { for ($x = 0; $x -lt 100; $x++) {
    $on = ((([Math]::Floor($x / 10)) + ([Math]::Floor($y / 10))) % 2) -eq 0
    $bmp.SetPixel($x, $y, $(if ($on) { [System.Drawing.Color]::FromArgb(255, 245, 160, 60) } else { [System.Drawing.Color]::FromArgb(255, 70, 50, 40) })) } }
$bmp.Save((Join-Path $OutDir "DualityBot_Checker.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

$gltf = [ordered]@{
    asset = [ordered]@{ version = "2.0"; generator = "DualityEngine sample generator (CC0)" }
    scene = 0
    scenes = @([ordered]@{ nodes = @(0) })
    nodes = @(
        [ordered]@{ name = "DualityBot"; children = @(1, 2, 7) },
        [ordered]@{ name = "Body"; mesh = 0 },
        [ordered]@{ name = "Joint0"; translation = @(0.62, 0.05, 0); children = @(3) },
        [ordered]@{ name = "Joint1"; translation = @(0, 0.4, 0); children = @(4) },
        [ordered]@{ name = "Joint2"; translation = @(0, 0.4, 0); children = @(5) },
        [ordered]@{ name = "Joint3"; translation = @(0, 0.4, 0) },
        [ordered]@{ name = "Unused" },
        [ordered]@{ name = "Arm"; mesh = 1; skin = 0 })
    skins = @([ordered]@{ joints = @(2, 3, 4, 5); inverseBindMatrices = $ibmAcc })
    meshes = @(
        [ordered]@{ name = "Body"; primitives = @([ordered]@{ attributes = [ordered]@{ POSITION = $bodyPos; NORMAL = $bodyNrm; TEXCOORD_0 = $bodyUv }; indices = $bodyIdx; material = 0 }) },
        [ordered]@{ name = "Arm"; primitives = @([ordered]@{ attributes = [ordered]@{ POSITION = $armPos; NORMAL = $armNrm; TEXCOORD_0 = $armUv; JOINTS_0 = $armJ; WEIGHTS_0 = $armW }; indices = $armIdx; material = 1 }) })
    materials = @(
        [ordered]@{ name = "BotBody"; pbrMetallicRoughness = [ordered]@{ baseColorFactor = @(1, 1, 1, 1); baseColorTexture = [ordered]@{ index = 0 } } },
        [ordered]@{ name = "BotArm"; pbrMetallicRoughness = [ordered]@{ baseColorFactor = @(0.25, 0.55, 1.0, 1) }; emissiveFactor = @(0.05, 0.1, 0.25) })
    textures = @([ordered]@{ source = 0 })
    images = @([ordered]@{ uri = "DualityBot_Checker.png" })
    animations = @(
        [ordered]@{ name = "Wave"; samplers = $samplers; channels = $channels },
        [ordered]@{ name = "Spin"; samplers = @([ordered]@{ input = $timeAcc; output = $spinAcc; interpolation = "LINEAR" }); channels = @([ordered]@{ sampler = 0; target = [ordered]@{ node = 0; path = "rotation" } }) })
    buffers = @([ordered]@{ byteLength = $buffer.Count; uri = "data:application/octet-stream;base64," + [Convert]::ToBase64String($buffer.ToArray()) })
    bufferViews = $views
    accessors = $accessors
}
$json = $gltf | ConvertTo-Json -Depth 12 -Compress
[IO.File]::WriteAllText((Join-Path $OutDir "DualityBot.gltf"), $json, (New-Object System.Text.UTF8Encoding $false))
"wrote $OutDir"
