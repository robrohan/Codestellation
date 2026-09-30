package main

import (
	"fmt"

	"example.com/demo/greet"
	"example.com/demo/store"
)

func main() {
	s := store.New()
	fmt.Println(greet.Hello("world", s))
}
