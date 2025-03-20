package godbc

import (
	"context"
	"fmt"
)

// Driver represents a database driver
type Driver interface {
	// Connect establishes a connection to the database
	Connect(ctx context.Context, dsn string) (Connection, error)
}

var drivers = make(map[string]Driver)

// Register registers a database driver
func Register(name string, driver Driver) {
	drivers[name] = driver
}

// Connect creates a new database connection using the specified driver
func Connect(ctx context.Context, driverName, dsn string) (Connection, error) {
	driver, ok := drivers[driverName]
	if !ok {
		return nil, fmt.Errorf("driver %s not registered", driverName)
	}
	return driver.Connect(ctx, dsn)
}
