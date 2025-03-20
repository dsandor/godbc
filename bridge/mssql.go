package main

import (
	"context"
	"database/sql"
	"fmt"
	"time"
)

type Config struct {
	MinConnections int
	MaxConnections int
	ConnectTimeout time.Duration
	RetryDelay     time.Duration
	RetryAttempts  int
}

func OpenMSSQL(connStr string, config *Config) (*sql.DB, error) {
	if config == nil {
		config = &Config{
			MinConnections: 1,
			MaxConnections: 10,
			ConnectTimeout: 30 * time.Second,
			RetryDelay:     time.Second,
			RetryAttempts:  3,
		}
	}

	var db *sql.DB
	var err error

	for attempt := 0; attempt <= config.RetryAttempts; attempt++ {
		if attempt > 0 {
			time.Sleep(config.RetryDelay)
		}

		db, err = sql.Open("sqlserver", connStr)
		if err != nil {
			continue
		}

		db.SetMaxOpenConns(config.MaxConnections)
		db.SetMaxIdleConns(config.MinConnections)
		db.SetConnMaxLifetime(config.ConnectTimeout)

		// Test the connection
		ctx, cancel := context.WithTimeout(context.Background(), config.ConnectTimeout)
		defer cancel()

		err = db.PingContext(ctx)
		if err == nil {
			return db, nil
		}

		db.Close()
	}

	if err != nil {
		return nil, fmt.Errorf("failed to connect after %d attempts: %v", config.RetryAttempts+1, err)
	}

	return db, nil
}

func GetOrCreatePool(connStr string, config *Config) (*Pool, error) {
	mu.Lock()
	defer mu.Unlock()

	if pool, exists := connPool[connStr]; exists {
		return pool, nil
	}

	db, err := OpenMSSQL(connStr, config)
	if err != nil {
		return nil, err
	}

	pool := &Pool{
		db:            db,
		minConns:      config.MinConnections,
		maxConns:      config.MaxConnections,
		connTimeout:   config.ConnectTimeout,
		retryDelay:    config.RetryDelay,
		retryAttempts: config.RetryAttempts,
	}

	connPool[connStr] = pool
	return pool, nil
}
