package main

import (
	"crypto/rand"
	"errors"
	"math/big"
	"time"
)

// Slot is one combatant position. Owner is the connection that controls it (0 = none).
// PrevOwnerToken is the reconnect token of a human who dropped and may still reclaim it.
type Slot struct {
	State          SlotState
	Owner          uint64
	Username       string
	PrevOwnerToken string
	PrevUsername   string
	Deadline       time.Time // grace expiry (DISCONNECTED/BOT_TAKEOVER) or handoff/reservation expiry
}

type Room struct {
	ID         string
	Code       string // private rooms only
	Public     bool
	HostCID    uint64
	Mode       string
	Map        string
	MaxPlayers int
	Settings   map[string]string
	Status     RoomStatus
	Slots      []Slot
	Created    time.Time
	// Teams: the first half of the slots is team 1, the second half team 2, so a 16-slot
	// room is 8 vs 8 by construction and a slot (and the bot that may take it over) never
	// changes team. Off for free-for-all, where all slots are one pool.
	Teams bool
}

// TeamOf returns 0 in free-for-all, else 1 or 2.
func (r *Room) TeamOf(slot int) int {
	if !r.Teams {
		return 0
	}
	if slot < r.MaxPlayers/2 {
		return 1
	}
	return 2
}

func occupied(s Slot) bool { return s.State != SlotEmpty }

// teamFill counts occupied slots (humans and replacement bots) per team.
func (r *Room) teamFill() [3]int {
	var n [3]int
	for i, s := range r.Slots {
		if occupied(s) {
			n[r.TeamOf(i)]++
		}
	}
	return n
}

// slotOrder is the order Admit tries slots in: free-for-all in index order; with teams,
// the requested team first if it has room, else the emptier team (team 1 on a tie).
func (r *Room) slotOrder(pref int) []int {
	order := make([]int, 0, len(r.Slots))
	if !r.Teams {
		for i := range r.Slots {
			order = append(order, i)
		}
		return order
	}
	fill := r.teamFill()
	half := r.MaxPlayers / 2
	first := 1
	if fill[2] < fill[1] {
		first = 2
	}
	if (pref == 1 || pref == 2) && fill[pref] < half {
		first = pref
	}
	for _, t := range []int{first, 3 - first} {
		lo := 0
		if t == 2 {
			lo = half
		}
		for i := lo; i < lo+half; i++ {
			order = append(order, i)
		}
	}
	return order
}

var (
	ErrRoomFull     = errors.New("room_full")
	ErrNoSuchSlot   = errors.New("no_such_slot")
	ErrBadState     = errors.New("bad_state")
	ErrAlreadyOwned = errors.New("slot_already_owned")
)

// Timing policy (brief sections 15-17). Tuned in config, defaults here.
type Timing struct {
	ReconnectGrace time.Duration // how long a dropped human keeps first claim on their slot
	HandoffTimeout time.Duration // how long a joiner has to finish sync before the slot reverts
	ReserveTimeout time.Duration // how long a pre-match reservation may sit unconfirmed
}

const codeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" // no 0/O/1/I

func randomString(n int, alphabet string) string {
	b := make([]byte, n)
	max := big.NewInt(int64(len(alphabet)))
	for i := range b {
		v, err := rand.Int(rand.Reader, max)
		if err != nil {
			panic(err)
		}
		b[i] = alphabet[v.Int64()]
	}
	return string(b)
}

// NewRoomCode returns e.g. GHOST-7K4P9Q: 32^6 ~ 1.07e9 codes, and code guesses are rate limited.
func NewRoomCode() string { return "GHOST-" + randomString(6, codeAlphabet) }

func NewRoom(id string, public bool, host uint64, hostName, mode, mapName string, max int, settings map[string]string, teams bool) *Room {
	r := &Room{ID: id, Public: public, HostCID: host, Mode: mode, Map: mapName, MaxPlayers: max,
		Settings: settings, Status: StatusLobby, Slots: make([]Slot, max), Created: time.Now(), Teams: teams}
	for i := range r.Slots {
		r.Slots[i].State = SlotEmpty
	}
	r.Slots[0] = Slot{State: SlotHuman, Owner: host, Username: hostName}
	return r
}

func (r *Room) SlotOf(cid uint64) int {
	for i, s := range r.Slots {
		if s.Owner == cid && cid != 0 {
			return i
		}
	}
	return -1
}

func (r *Room) Counts() (humans, bots, joinable int) {
	for _, s := range r.Slots {
		switch s.State {
		case SlotHuman, SlotHumanActive, SlotReserved, SlotHumanRejoining:
			humans++
		case SlotBotTakeover:
			bots++
			if s.PrevOwnerToken == "" { // grace over: open to a new human
				joinable++
			}
		case SlotEmpty:
			// Empty slots in a running match stay empty unless they were vacated by a human
			// (that path goes through BOT_TAKEOVER); new humans may still take them.
			joinable++
		}
	}
	return
}

// Admit places a new human. Preference: an empty lobby slot, else (match running) an open
// bot-takeover slot, which then goes through HUMAN_REJOINING until the host confirms handoff.
// pref asks for a team (1 or 2, 0 = any); it is honoured when that team has room.
func (r *Room) Admit(cid uint64, name string, pref int, t Timing, now time.Time) (int, SlotState, error) {
	if r.SlotOf(cid) >= 0 {
		return -1, "", ErrAlreadyOwned
	}
	order := r.slotOrder(pref)
	for _, i := range order {
		if r.Slots[i].State == SlotEmpty {
			st := SlotHuman
			if r.Status == StatusInProgress {
				st = SlotHumanRejoining // needs a snapshot before control
			}
			r.Slots[i] = Slot{State: st, Owner: cid, Username: name, Deadline: now.Add(t.HandoffTimeout)}
			return i, st, nil
		}
	}
	if r.Status == StatusInProgress {
		for _, i := range order {
			s := &r.Slots[i]
			if s.State == SlotBotTakeover && s.PrevOwnerToken == "" {
				s.State, s.Owner, s.Username, s.Deadline = SlotHumanRejoining, cid, name, now.Add(t.HandoffTimeout)
				return i, s.State, nil
			}
		}
	}
	return -1, "", ErrRoomFull
}

// Drop handles a lost or leaving connection. In a running match the slot goes to the
// original AI immediately (DISCONNECTED -> BOT_TAKEOVER is reported as one step to the host,
// which spawns the bot controller) and the player keeps first claim until the grace expires.
func (r *Room) Drop(cid uint64, token string, t Timing, now time.Time) (int, SlotState) {
	i := r.SlotOf(cid)
	if i < 0 {
		return -1, ""
	}
	s := &r.Slots[i]
	switch {
	case r.Status == StatusInProgress && (s.State == SlotHumanActive || s.State == SlotHuman):
		*s = Slot{State: SlotBotTakeover, PrevOwnerToken: token, PrevUsername: s.Username,
			Deadline: now.Add(t.ReconnectGrace)}
	case s.State == SlotHumanRejoining && s.PrevUsername != "":
		// A joiner who was replacing a bot vanished mid-sync: the bot keeps playing.
		*s = Slot{State: SlotBotTakeover}
	default:
		*s = Slot{State: SlotEmpty}
	}
	return i, s.State
}

// Reclaim lets a dropped human take their slot back within the grace period.
func (r *Room) Reclaim(cid uint64, token, name string, t Timing, now time.Time) (int, error) {
	for i := range r.Slots {
		s := &r.Slots[i]
		if s.State == SlotBotTakeover && s.PrevOwnerToken != "" && s.PrevOwnerToken == token {
			*s = Slot{State: SlotHumanRejoining, Owner: cid, Username: name, PrevUsername: s.PrevUsername,
				Deadline: now.Add(t.HandoffTimeout)}
			return i, nil
		}
	}
	return -1, ErrNoSuchSlot
}

// SwitchTeam moves a human to an empty slot on the other team, in the lobby only (a running
// match keeps teams fixed so scores and objectives stay consistent).
func (r *Room) SwitchTeam(cid uint64) (int, error) {
	from := r.SlotOf(cid)
	if from < 0 {
		return -1, ErrNoSuchSlot
	}
	if !r.Teams || r.Status != StatusLobby {
		return -1, ErrBadState
	}
	other := 3 - r.TeamOf(from)
	for i := range r.Slots {
		if r.TeamOf(i) == other && r.Slots[i].State == SlotEmpty {
			r.Slots[i], r.Slots[from] = r.Slots[from], Slot{State: SlotEmpty}
			return i, nil
		}
	}
	return -1, ErrRoomFull
}

// ConfirmHandoff is sent by the host once the joiner has the snapshot and the bot controller
// is detached at a safe point. Only then does the human gain control.
func (r *Room) ConfirmHandoff(slot int) error {
	if slot < 0 || slot >= len(r.Slots) {
		return ErrNoSuchSlot
	}
	s := &r.Slots[slot]
	if s.State != SlotHumanRejoining || s.Owner == 0 {
		return ErrBadState
	}
	s.State, s.Deadline, s.PrevUsername = SlotHumanActive, time.Time{}, ""
	return nil
}

// Start moves lobby humans into the running state. Empty slots stay empty: no bot filler.
func (r *Room) Start() {
	r.Status = StatusInProgress
	for i := range r.Slots {
		if r.Slots[i].State == SlotHuman {
			r.Slots[i].State = SlotHumanActive
		}
	}
}

// Tick expires grace periods and stalled handoffs. Returns the slots that changed.
func (r *Room) Tick(now time.Time) []int {
	var changed []int
	for i := range r.Slots {
		s := &r.Slots[i]
		if s.Deadline.IsZero() || now.Before(s.Deadline) {
			continue
		}
		switch s.State {
		case SlotBotTakeover:
			if s.PrevOwnerToken != "" { // reconnect reservation over: open to anyone
				s.PrevOwnerToken, s.PrevUsername, s.Deadline = "", "", time.Time{}
				changed = append(changed, i)
			}
		case SlotHumanRejoining:
			// Handoff never confirmed. Bot keeps the combatant; joiner is removed by the server.
			*s = Slot{State: SlotBotTakeover}
			changed = append(changed, i)
		}
	}
	return changed
}

func (r *Room) Info(hostName string, withSlots bool) RoomInfo {
	h, b, j := r.Counts()
	ri := RoomInfo{ID: r.ID, Host: hostName, Mode: r.Mode, Map: r.Map, Humans: h, Bots: b,
		MaxPlayers: r.MaxPlayers, Joinable: j, Status: r.Status, Public: r.Public, Settings: r.Settings, Teams: r.Teams}
	if r.Teams {
		f := r.teamFill()
		ri.TeamFill = []int{f[1], f[2]}
	}
	if withSlots {
		for i, s := range r.Slots {
			name := s.Username
			if s.State == SlotBotTakeover {
				name = "Replacement Bot"
			}
			ri.Slots = append(ri.Slots, SlotInfo{Slot: i, State: s.State, Username: name, IsHost: s.Owner == r.HostCID && s.Owner != 0, Team: r.TeamOf(i)})
		}
	}
	return ri
}
