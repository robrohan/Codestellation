<?php

namespace App;

use Shapes\Circle;
use Shapes\{ShapeRunnerBase, Square};

require_once 'helpers.php';

class Program extends ShapeRunnerBase
{
    public function run(): void
    {
        $shapes = [new Circle(2), new Square(3)];
        foreach ($shapes as $shape) {
            echo format_area($shape->area()), PHP_EOL;
        }
    }
}
