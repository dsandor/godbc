package godbc

import "errors"

var (
	// ErrConnClosed is returned when attempting to use a closed connection
	ErrConnClosed = errors.New("connection is closed")
	
	// ErrTxDone is returned when attempting to use a committed or rolled back transaction
	ErrTxDone = errors.New("transaction has already been committed or rolled back")
	
	// ErrStmtClosed is returned when attempting to use a closed statement
	ErrStmtClosed = errors.New("statement is closed")
	
	// ErrNoRows is returned when no rows are found
	ErrNoRows = errors.New("no rows in result set")
)
