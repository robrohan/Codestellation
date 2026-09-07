package greet

import "example.com/demo/store"

// Hello greets name, tagging on the store's label.
func Hello(name string, s *store.Store) string {
	return "Hello, " + name + " (" + s.Label() + ")"
}
