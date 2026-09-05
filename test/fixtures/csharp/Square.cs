namespace CodeMap.Shapes
{
    public class Square : CodeMap.Shapes.IShape
    {
        public double Side;

        public double Area() => Side * Side;
    }
}
