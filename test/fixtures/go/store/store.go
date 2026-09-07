package store

import "example.com/demo/internal/util"

type Store struct{ name string }

func New() *Store { return &Store{name: "default"} }

func (s *Store) Label() string { return util.Title(s.name) }
