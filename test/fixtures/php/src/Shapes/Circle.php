<?php

namespace Shapes;

class Circle implements IShape
{
    public function __construct(private float $radius)
    {
    }

    public function area(): float
    {
        return 3.14159 * $this->radius * $this->radius;
    }
}
