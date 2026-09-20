<?php

/* Block-form namespace (the other files use the statement form) and a
 * fully-qualified leading-backslash reference, to exercise both. */
namespace Shapes {
    class Square implements \Shapes\IShape
    {
        public function __construct(private float $side)
        {
        }

        public function area(): float
        {
            return $this->side * $this->side;
        }
    }
}
