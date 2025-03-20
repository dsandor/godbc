package mssql

import (
	"context"
	"database/sql"
	"github.com/dsandor/godbc"
)

type result struct {
	sqlResult sql.Result
}

type sqlRows struct {
	sqlRows *sql.Rows
}

type statement struct {
	stmt *sql.Stmt
}

type transaction struct {
	tx *sql.Tx
}

func (c *connection) Execute(ctx context.Context, query string, args ...interface{}) (godbc.Result, error) {
	res, err := c.db.ExecContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	return &result{sqlResult: res}, nil
}

func (c *connection) Query(ctx context.Context, query string, args ...interface{}) (godbc.Rows, error) {
	rows, err := c.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	return &sqlRows{sqlRows: rows}, nil
}

func (c *connection) Prepare(ctx context.Context, query string) (godbc.Statement, error) {
	stmt, err := c.db.PrepareContext(ctx, query)
	if err != nil {
		return nil, err
	}
	return &statement{stmt: stmt}, nil
}

func (c *connection) Begin(ctx context.Context) (godbc.Transaction, error) {
	tx, err := c.db.BeginTx(ctx, nil)
	if err != nil {
		return nil, err
	}
	return &transaction{tx: tx}, nil
}

func (c *connection) Close() error {
	return c.db.Close()
}

func (r *result) LastInsertId() (int64, error) {
	return r.sqlResult.LastInsertId()
}

func (r *result) RowsAffected() (int64, error) {
	return r.sqlResult.RowsAffected()
}

func (r *sqlRows) Next() bool {
	return r.sqlRows.Next()
}

func (r *sqlRows) Scan(dest ...interface{}) error {
	return r.sqlRows.Scan(dest...)
}

func (r *sqlRows) Close() error {
	return r.sqlRows.Close()
}

func (s *statement) Execute(ctx context.Context, args ...interface{}) (godbc.Result, error) {
	res, err := s.stmt.ExecContext(ctx, args...)
	if err != nil {
		return nil, err
	}
	return &result{sqlResult: res}, nil
}

func (s *statement) Query(ctx context.Context, args ...interface{}) (godbc.Rows, error) {
	rows, err := s.stmt.QueryContext(ctx, args...)
	if err != nil {
		return nil, err
	}
	return &sqlRows{sqlRows: rows}, nil
}

func (s *statement) Close() error {
	return s.stmt.Close()
}

func (t *transaction) Execute(ctx context.Context, query string, args ...interface{}) (godbc.Result, error) {
	res, err := t.tx.ExecContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	return &result{sqlResult: res}, nil
}

func (t *transaction) Query(ctx context.Context, query string, args ...interface{}) (godbc.Rows, error) {
	rows, err := t.tx.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	return &sqlRows{sqlRows: rows}, nil
}

func (t *transaction) Prepare(ctx context.Context, query string) (godbc.Statement, error) {
	stmt, err := t.tx.PrepareContext(ctx, query)
	if err != nil {
		return nil, err
	}
	return &statement{stmt: stmt}, nil
}

func (t *transaction) Begin(ctx context.Context) (godbc.Transaction, error) {
	return nil, godbc.ErrTxDone
}

func (t *transaction) Close() error {
	return t.Rollback()
}

func (t *transaction) Commit() error {
	return t.tx.Commit()
}

func (t *transaction) Rollback() error {
	return t.tx.Rollback()
}
