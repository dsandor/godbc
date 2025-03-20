package godbc

import (
	"context"
)

// Connection represents a database connection
type Connection interface {
	// Execute executes a query without returning any rows
	Execute(ctx context.Context, query string, args ...interface{}) (Result, error)
	
	// Query executes a query that returns rows
	Query(ctx context.Context, query string, args ...interface{}) (Rows, error)
	
	// Prepare creates a prepared statement
	Prepare(ctx context.Context, query string) (Statement, error)
	
	// Begin starts a new transaction
	Begin(ctx context.Context) (Transaction, error)
	
	// Close closes the connection
	Close() error
}

// Result represents the result of a query execution
type Result interface {
	// LastInsertId returns the ID of the last inserted row
	LastInsertId() (int64, error)
	
	// RowsAffected returns the number of rows affected by the query
	RowsAffected() (int64, error)
}

// Rows represents a result set
type Rows interface {
	// Next prepares the next row for reading
	Next() bool
	
	// Scan copies the columns in the current row into the values pointed at by dest
	Scan(dest ...interface{}) error
	
	// Close closes the rows iterator
	Close() error
}

// Statement represents a prepared statement
type Statement interface {
	// Execute executes the prepared statement
	Execute(ctx context.Context, args ...interface{}) (Result, error)
	
	// Query executes the prepared statement and returns the rows
	Query(ctx context.Context, args ...interface{}) (Rows, error)
	
	// Close closes the statement
	Close() error
}

// Transaction represents a database transaction
type Transaction interface {
	Connection
	
	// Commit commits the transaction
	Commit() error
	
	// Rollback aborts the transaction
	Rollback() error
}
