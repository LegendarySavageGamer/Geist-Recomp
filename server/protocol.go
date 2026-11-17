// Package main is geist-online-relay: session discovery, room membership and packet routing
// for GeistNative online multiplayer. The game host is authoritative for the simulation; the
// relay never simulates gameplay. It decides only who is in which room and slot.
package main

// Wire format (UDP, big-endian), one datagram per message:
//
//	magic   [4]byte "GST1"
//	kind    uint8   (KindControl | KindGame)
//	flags   uint8   (reserved, 0)
//	cid     uint64  connection id issued by WELCOME (0 before HELLO completes)
//	seq     uint32  per-connection, strictly increasing for control messages (replay guard)
//	payload ...     control: JSON object {"op": ..., ...}; game: 1-byte target + opaque bytes
//
// Game payloads are opaque to the relay. A client may only address the host (target 0xFE);
// the host may address one slot (0..MaxSlots-1) or everyone (0xFF). That keeps the host the
// single authority for the match without the relay having to understand game state.

const (
	Magic         = "GST1"
	HeaderSize    = 4 + 1 + 1 + 8 + 4
	KindControl   = 1
	KindGame      = 2
	TargetHost    = 0xFE
	TargetAll     = 0xFF
	MaxDatagram   = 1400
	ProtocolVer   = 1
	MaxSlotsLimit = 16 // 16 free-for-all, or 8 vs 8
)

// SlotState is the explicit per-slot state machine (brief section 15).
type SlotState string

const (
	SlotEmpty          SlotState = "EMPTY"
	SlotReserved       SlotState = "RESERVED"        // a human is connecting into this slot
	SlotHuman          SlotState = "HUMAN"           // in lobby, before the match runs
	SlotDisconnected   SlotState = "DISCONNECTED"    // connection lost, grace period running
	SlotBotTakeover    SlotState = "BOT_TAKEOVER"    // host runs the original AI in this slot
	SlotHumanRejoining SlotState = "HUMAN_REJOINING" // a human is being synced into a bot slot
	SlotHumanActive    SlotState = "HUMAN_ACTIVE"    // a human controls the slot in a running match
)

type RoomStatus string

const (
	StatusLobby      RoomStatus = "LOBBY"
	StatusInProgress RoomStatus = "IN_PROGRESS"
	StatusEnded      RoomStatus = "ENDED"
)

// Control is the JSON control envelope; unused fields are omitted.
type Control struct {
	Op string `json:"op"`

	// HELLO / WELCOME / REJOIN
	Version        int    `json:"ver,omitempty"`
	Build          string `json:"build,omitempty"`
	Username       string `json:"username,omitempty"`
	CID            string `json:"cid,omitempty"`
	ReconnectToken string `json:"token,omitempty"`

	// PING / PONG
	T int64 `json:"t,omitempty"`

	// HOST / room info
	Public     *bool             `json:"public,omitempty"`
	Mode       string            `json:"mode,omitempty"`
	Map        string            `json:"map,omitempty"`
	MaxPlayers int               `json:"max,omitempty"`
	Settings   map[string]string `json:"settings,omitempty"`

	// HOST: team play (forced on for capture_the_host and hunt); JOIN/SWITCH_TEAM: wanted team
	Teams *bool `json:"teams,omitempty"`
	Team  int   `json:"team,omitempty"`

	// JOIN
	RoomID string `json:"room,omitempty"`
	Code   string `json:"code,omitempty"`

	// LIST filters
	Search   string `json:"search,omitempty"`
	HideFull bool   `json:"hide_full,omitempty"`

	// Responses
	Slot   *int       `json:"slot,omitempty"`
	Rooms  []RoomInfo `json:"rooms,omitempty"`
	Room   *RoomInfo  `json:"room_info,omitempty"`
	Error  string     `json:"error,omitempty"`
	Reason string     `json:"reason,omitempty"`
	State  SlotState  `json:"state,omitempty"`
	Status RoomStatus `json:"status,omitempty"`
}

type SlotInfo struct {
	Slot     int       `json:"slot"`
	State    SlotState `json:"state"`
	Username string    `json:"username,omitempty"`
	IsHost   bool      `json:"host,omitempty"`
	Team     int       `json:"team,omitempty"` // 0 free-for-all, 1 or 2
}

// RoomInfo is what the public browser and lobby screen show. Ping is measured by the client
// (relay round trip), never filled in by the server.
type RoomInfo struct {
	ID         string            `json:"id"`
	Host       string            `json:"host"`
	Mode       string            `json:"mode"`
	Map        string            `json:"map"`
	Humans     int               `json:"humans"`
	Bots       int               `json:"bots"`
	MaxPlayers int               `json:"max"`
	Joinable   int               `json:"joinable"`
	Status     RoomStatus        `json:"status"`
	Public     bool              `json:"public"`
	Teams      bool              `json:"teams"`
	TeamFill   []int             `json:"team_fill,omitempty"` // [team1, team2] incl. replacement bots
	Settings   map[string]string `json:"settings,omitempty"`
	Slots      []SlotInfo        `json:"slots,omitempty"`
}
