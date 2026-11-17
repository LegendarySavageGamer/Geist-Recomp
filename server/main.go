package main

import (
	"crypto/rand"
	"log/slog"
	"net"
	"os"
	"strconv"
	"time"
)

var randRead = rand.Read

func env(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func envInt(k string, def int) int {
	if v, err := strconv.Atoi(os.Getenv(k)); err == nil {
		return v
	}
	return def
}

func main() {
	log := slog.New(slog.NewTextHandler(os.Stdout, nil)).With("svc", "geist-online-relay")
	cfg := Config{
		Listen:         env("GEIST_LISTEN", ":47720"),
		MaxPlayers:     envInt("GEIST_MAX_PLAYERS", MaxSlotsLimit),
		DefaultPlayers: envInt("GEIST_DEFAULT_PLAYERS", 16),
		MinBuild:       os.Getenv("GEIST_REQUIRE_BUILD"),
		Timing: Timing{
			ReconnectGrace: time.Duration(envInt("GEIST_RECONNECT_GRACE_S", 45)) * time.Second,
			HandoffTimeout: time.Duration(envInt("GEIST_HANDOFF_TIMEOUT_S", 20)) * time.Second,
			ReserveTimeout: 20 * time.Second,
		},
		ConnTimeout:     time.Duration(envInt("GEIST_CONN_TIMEOUT_S", 10)) * time.Second,
		PacketsPerSec:   float64(envInt("GEIST_PPS", 200)),
		CodeTriesPerMin: float64(envInt("GEIST_CODE_TRIES_PER_MIN", 10)),
		MaxRooms:        envInt("GEIST_MAX_ROOMS", 256),
	}
	if cfg.MaxPlayers > MaxSlotsLimit {
		cfg.MaxPlayers = MaxSlotsLimit
	}
	addr, err := net.ResolveUDPAddr("udp", cfg.Listen)
	if err != nil {
		log.Error("bad listen address", "err", err)
		os.Exit(1)
	}
	sock, err := net.ListenUDP("udp", addr)
	if err != nil {
		log.Error("listen failed", "err", err)
		os.Exit(1)
	}
	log.Info("listening", "addr", cfg.Listen, "max_players", cfg.MaxPlayers, "proto", ProtocolVer)
	NewServer(cfg, log).Serve(sock)
}
