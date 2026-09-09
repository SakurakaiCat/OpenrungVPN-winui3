// openrung-core is the sidecar process for the OpenRung WinUI3 desktop app.
// It wraps the connectcore engine behind a loopback HTTP/SSE API the C# app
// drives; the exact contract lives in ../API-CONTRACT.md.
package main

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"time"

	"github.com/openrung/openrung/connectcore/client"

	"openrung/internal/singboxruntime"
)

func main() {
	os.Exit(run(os.Args[1:]))
}

func run(args []string) int {
	// Subcommand dispatch happens before anything can bind a socket: the
	// engine starts a tunnel by re-executing this binary as `run -c <config>`,
	// and that child must never reach the API server. `version` is likewise a
	// pure print for packaging checks.
	if len(args) > 0 {
		switch args[0] {
		case singboxruntime.Subcommand:
			if err := singboxruntime.RunSubcommand(args[1:]); err != nil {
				fmt.Fprintln(os.Stderr, err)
				return 1
			}
			return 0
		case "version", "-version", "--version":
			fmt.Printf("openrung-core/%s\n%s\n", client.AppVersion(), singboxruntime.VersionLine())
			return 0
		case "serve":
			args = args[1:]
		}
	}

	fs := flag.NewFlagSet("serve", flag.ContinueOnError)
	port := fs.Int("port", 0, "loopback port to bind (0 = ephemeral; otherwise OPENRUNG_CORE_PORT applies)")
	token := fs.String("token", "", "auth token for the API (default: generated)")
	heartbeatTimeout := fs.Duration("heartbeat-timeout", 0,
		"graceful exit when no API request arrives within this window (0 = disabled)")
	broker := fs.String("broker", "", "default broker URL when /api/connect and /api/relays pass none")
	if err := fs.Parse(args); err != nil {
		return 2
	}

	// The flag wins; the env var covers launchers that cannot rewrite argv.
	if *port == 0 {
		if v := os.Getenv("OPENRUNG_CORE_PORT"); v != "" {
			n, err := strconv.Atoi(v)
			if err != nil || n <= 0 || n > 65535 {
				fmt.Fprintf(os.Stderr, "invalid OPENRUNG_CORE_PORT %q\n", v)
				return 2
			}
			*port = n
		}
	}

	cfg := serveConfig{
		port:             *port,
		token:            *token,
		heartbeatTimeout: *heartbeatTimeout,
		brokerURL:        *broker,
	}
	if err := serve(cfg); err != nil {
		fmt.Fprintln(os.Stderr, err)
		if exitErr, ok := err.(*exitError); ok {
			return exitErr.code
		}
		return 1
	}
	return 0
}

type serveConfig struct {
	port             int
	token            string
	heartbeatTimeout time.Duration
	brokerURL        string
}
