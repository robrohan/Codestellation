using System;
using CodeMap.Shapes;

namespace CodeMap.App;

public class Program : ShapeRunnerBase
{
    public override void Run()
    {
        var c = new Circle { Radius = 2 };
        Console.WriteLine(c.Area());
    }
}
