Namespace App.Shapes
    Public Class Square
        Implements App.Shapes.IShape

        Public Side As Double

        Public Function Area() As Double Implements IShape.Area
            Return Side * Side
        End Function
    End Class
End Namespace
