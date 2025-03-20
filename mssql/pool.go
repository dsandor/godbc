package mssql

import (
	"context"
	"sync"

	"github.com/dsandor/godbc"
)

// PoolManager manages multiple connection pools
type PoolManager struct {
	mu    sync.RWMutex
	pools map[string]*Pool
}

// Pool represents a connection pool for a specific connection string
type Pool struct {
	config     *Config
	driver     *Driver
	connString string
}

var (
	globalPoolManager = &PoolManager{
		pools: make(map[string]*Pool),
	}
)

// GetOrCreatePool returns an existing pool for the connection string or creates a new one
func GetOrCreatePool(connString string, config *Config) (*Pool, error) {
	globalPoolManager.mu.Lock()
	defer globalPoolManager.mu.Unlock()

	// Check if pool exists
	if pool, exists := globalPoolManager.pools[connString]; exists {
		return pool, nil
	}

	// Create new pool
	driver := NewDriver(config)
	pool := &Pool{
		config:     config,
		driver:     driver,
		connString: connString,
	}

	globalPoolManager.pools[connString] = pool
	return pool, nil
}

// Connect creates a new connection from the pool
func (p *Pool) Connect(ctx context.Context) (godbc.Connection, error) {
	return p.driver.Connect(ctx, p.connString)
}

// Close closes all connections in the pool
func (p *Pool) Close() error {
	globalPoolManager.mu.Lock()
	defer globalPoolManager.mu.Unlock()

	delete(globalPoolManager.pools, p.connString)
	return nil
}
