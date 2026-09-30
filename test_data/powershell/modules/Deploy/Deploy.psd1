# Module manifest: the entries below reference other files.
@{
    ModuleVersion = '1.0.0'
    RootModule    = 'Deploy.psm1'                # -> Deploy.psm1
    NestedModules = @('..\Tools.psm1')           # -> modules/Tools.psm1
}
