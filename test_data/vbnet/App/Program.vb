Imports App.Shapes

Namespace App
    Public Class Program
        Inherits ShapeRunnerBase

        Public Overrides Sub Run()
            Dim c As New Circle()
            c.Radius = 2
            System.Console.WriteLine(c.Area())
        End Sub
    End Class
End Namespace
