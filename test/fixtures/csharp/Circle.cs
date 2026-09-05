namespace CodeMap.Shapes
{
    public class Circle : IShape
    {
        public double Radius;

        public double Area() => 3.14159 * Radius * Radius;
    }
}
