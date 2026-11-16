param([string]$Winmd, [string]$Out)
# Lists every type of a .winmd with its methods and fields, in the format of knowledge/winmd/*.txt.
Add-Type -AssemblyName System.Reflection.Metadata
$stream = [IO.File]::OpenRead($Winmd)
$pe = [System.Reflection.PortableExecutable.PEReader]::new($stream)
$md = [System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
$entries = foreach ($h in $md.TypeDefinitions) {
    $t = $md.GetTypeDefinition($h)
    $name = $md.GetString($t.Name)
    if ($name -eq '<Module>') { continue }
    $full = $md.GetString($t.Namespace) + '.' + $name
    $methods = @($t.GetMethods() | ForEach-Object { $md.GetString($md.GetMethodDefinition($_).Name) })
    # An enum field carries its value: the values need not follow the order of the names (LyricsSource has two zeros).
    $fields = @($t.GetFields() | ForEach-Object {
            $field = $md.GetFieldDefinition($_)
            $fieldName = $md.GetString($field.Name)
            if ($fieldName -eq 'value__') { return }
            $constant = $field.GetDefaultValue()
            if ($constant.IsNil) { return $fieldName }
            $blob = $md.GetBlobReader($md.GetConstant($constant).Value)
            $fieldName + ' = ' + $blob.ReadConstant($md.GetConstant($constant).TypeCode)
        })
    $lines = @($full)
    if ($methods.Count) { $lines += '  methods: ' + ($methods -join ', ') }
    if ($fields.Count) { $lines += '  fields: ' + ($fields -join ', ') }
    [pscustomobject]@{ Key = $full; Text = $lines -join "`n" }
}
$sorted = [System.Collections.Generic.List[object]]::new($entries)
$sorted.Sort([Comparison[object]] { param($a, $b) [string]::CompareOrdinal($a.Key, $b.Key) })
[IO.File]::WriteAllText($Out, (($sorted | ForEach-Object Text) -join "`n") + "`n")
$pe.Dispose(); $stream.Dispose()
