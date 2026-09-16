# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
# EnhancerUI.ps1 - simple WinForms control panel: writes HKLM\SOFTWARE\EnhancerAPO (no elevation needed after register.ps1)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$key = 'HKLM:\SOFTWARE\EnhancerAPO'
$names  = @('Volume','HarmBass','HarmBassRange','DrumBass','DrumBassRange','Dry','HarmTreble','HarmTrebleRange','Ambience','AmbienceRange')
$labels = @('Volume','Harm Bass','Harm Bass Range','Drum Bass','Drum Bass Range','Dry Signal','Harm Treble','Harm Treble Range','Ambience','Ambience Range')
$presetFile = 'C:\Program Files (x86)\Winamp\Plugins\Enhancer\017\enhancer.set'
# "neutral": effects at 0, Volume 50 = 0 dB, Dry 100 = 0 dB
$neutral = @{Volume=50;HarmBass=0;HarmBassRange=50;DrumBass=0;DrumBassRange=50;Dry=100;HarmTreble=0;HarmTrebleRange=50;Ambience=0;AmbienceRange=50}

if (-not (Test-Path $key)) {
    [System.Windows.Forms.MessageBox]::Show("The key HKLM\SOFTWARE\EnhancerAPO does not exist yet. Run register.ps1 as Administrator first.", 'Enhancer APO') | Out-Null
    exit 1
}
function Fmt($name, $v) {
    switch ($name) {
        'Volume' { return ('{0}  ({1:+0.0;-0.0;0} dB)' -f $v, (0.4 * $v - 20)) }
        'Dry'    { return ('{0}  ({1:+0.0;-0.0;0} dB)' -f $v, (0.2 * $v - 20)) }
        default  { if ($v -eq 0 -and $name -notlike '*Range') { return '0 (off)' } else { return "$v" } }
    }
}

$script:updating = $false
$f = New-Object System.Windows.Forms.Form
$f.Text = 'Enhancer APO'; $f.Width = 470; $f.Height = 560; $f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false
$bars = @{}; $vals = @{}
$y = 12
for ($i = 0; $i -lt $names.Count; $i++) {
    $n = $names[$i]
    $l = New-Object System.Windows.Forms.Label; $l.Text = $labels[$i]; $l.Left = 12; $l.Top = $y + 6; $l.Width = 120; $f.Controls.Add($l)
    $t = New-Object System.Windows.Forms.TrackBar; $t.Left = 135; $t.Top = $y; $t.Width = 210; $t.Minimum = 0; $t.Maximum = 100; $t.TickFrequency = 10; $t.Tag = $n
    $t.Value = [int](Get-ItemProperty $key).$n
    $v = New-Object System.Windows.Forms.Label; $v.Left = 350; $v.Top = $y + 6; $v.Width = 100; $v.Text = (Fmt $n $t.Value); $f.Controls.Add($v)
    $bars[$n] = $t; $vals[$n] = $v
    $t.Add_ValueChanged({
        param($s, $e)
        $vals[$s.Tag].Text = (Fmt $s.Tag $s.Value)
        if (-not $script:updating) { Set-ItemProperty $key -Name $s.Tag -Value $s.Value }
    })
    $f.Controls.Add($t)
    $y += 42
}
$cbPower = New-Object System.Windows.Forms.CheckBox; $cbPower.Text = 'Power'; $cbPower.Left = 12; $cbPower.Top = $y + 4; $cbPower.Width = 70; $cbPower.Checked = ((Get-ItemProperty $key).Power -ne 0)
$cbPower.Add_CheckedChanged({ Set-ItemProperty $key -Name Power -Value ([int]$cbPower.Checked) }); $f.Controls.Add($cbPower)
$cbBoost = New-Object System.Windows.Forms.CheckBox; $cbBoost.Text = 'Boost'; $cbBoost.Left = 85; $cbBoost.Top = $y + 4; $cbBoost.Width = 70; $cbBoost.Checked = ((Get-ItemProperty $key).Boost -ne 0)
$cbBoost.Add_CheckedChanged({ Set-ItemProperty $key -Name Boost -Value ([int]$cbBoost.Checked) }); $f.Controls.Add($cbBoost)

$combo = New-Object System.Windows.Forms.ComboBox; $combo.Left = 160; $combo.Top = $y + 2; $combo.Width = 185; $combo.DropDownStyle = 'DropDownList'
$presets = [ordered]@{}
if (Test-Path $presetFile) {
    $lines = Get-Content $presetFile | Where-Object { $_ -ne '' }
    for ($i = 1; $i + 1 -lt $lines.Count; $i += 2) { $presets[$lines[$i]] = $lines[$i + 1]; [void]$combo.Items.Add($lines[$i]) }
}
function Apply-Values($h) {
    $script:updating = $true
    foreach ($n in $names) { $bars[$n].Value = [int]$h[$n] }
    $script:updating = $false
    foreach ($n in $names) { Set-ItemProperty $key -Name $n -Value ([int]$h[$n]) }
}
$combo.Add_SelectedIndexChanged({
    $vv = $presets[$combo.SelectedItem] -split ','
    $h = @{}; for ($i = 0; $i -lt $names.Count; $i++) { $h[$names[$i]] = [int]$vv[$i] }
    Apply-Values $h
})
$f.Controls.Add($combo)
$btn = New-Object System.Windows.Forms.Button; $btn.Text = 'Reset effects'; $btn.Left = 350; $btn.Top = $y; $btn.Width = 100
$btn.Add_Click({ Apply-Values $neutral; $combo.SelectedIndex = -1 }); $f.Controls.Add($btn)
$note = New-Object System.Windows.Forms.Label; $note.Left = 12; $note.Top = $y + 36; $note.Width = 440; $note.Height = 40
$note.Text = 'Reset effects = everything off, Volume and Dry at 0 dB (sound identical to the source). Changes apply within 0.3 s.'
$f.Controls.Add($note)
[void]$f.ShowDialog()
