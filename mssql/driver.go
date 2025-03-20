package mssql

import (
	"context"
	"database/sql"
	_ "github.com/denisenkom/go-mssqldb"
	"github.com/dsandor/godbc"
	"time"
)

const (
	defaultMinConnections = 1
	defaultMaxConnections = 10
	defaultConnTimeout   = 30 * time.Second
	defaultRetryAttempts = 3
	defaultRetryDelay    = 1 * time.Second
)

type Config struct {
	MinConnections int
	MaxConnections int
	ConnTimeout    time.Duration
	RetryAttempts  int
	RetryDelay     time.Duration
}

type Driver struct {
	config Config
}

type connection struct {
	db *sql.DB
}

func NewDriver(config *Config) *Driver {
	if config == nil {
		config = &Config{}
	}
	
	if config.MinConnections == 0 {
		config.MinConnections = defaultMinConnections
	}
	if config.MaxConnections == 0 {
		config.MaxConnections = defaultMaxConnections
	}
	if config.ConnTimeout == 0 {
		config.ConnTimeout = defaultConnTimeout
	}
	if config.RetryAttempts == 0 {
		config.RetryAttempts = defaultRetryAttempts
	}
	if config.RetryDelay == 0 {
		config.RetryDelay = defaultRetryDelay
	}
	
	return &Driver{config: *config}
}

func (d *Driver) Connect(ctx context.Context, dsn string) (godbc.Connection, error) {
	db, err := sql.Open("sqlserver", dsn)
	if err != nil {
		return nil, err
	}

	// Configure connection pool
	db.SetMaxOpenConns(d.config.MaxConnections)
	db.SetMaxIdleConns(d.config.MinConnections)
	
	// Test the connection with retry logic
	for attempt := 1; attempt <= d.config.RetryAttempts; attempt++ {
		ctxWithTimeout, cancel := context.WithTimeout(ctx, d.config.ConnTimeout)
		err = db.PingContext(ctxWithTimeout)
		cancel()
		
		if err == nil {
			break
		}
		
		if attempt == d.config.RetryAttempts {
			db.Close()
			return nil, err
		}
		
		time.Sleep(d.config.RetryDelay)
	}
	
	return &connection{db: db}, nil
}

func init() {
	// Register the MSSQL driver with default configuration
	godbc.Register("mssql", NewDriver(nil))
}
