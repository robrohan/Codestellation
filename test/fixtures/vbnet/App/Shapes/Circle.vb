' Implements/Inherits as their own statement (the standard VB.NET form,
' used throughout this fixture) is known not to produce type_reference
' edges with the currently pinned tree-sitter-vb-dotnet commit -- see
' vbnet_adapter.c's header comment for why.
Namespace App.Shapes
    Public Class Circle
        Implements IShape

        Public Radius As Double

        Public Function Area() As Double Implements IShape.Area
            Return 3.14159 * Radius * Radius
        End Function
    End Class
End Namespace
