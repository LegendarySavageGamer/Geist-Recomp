package main

import (
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"log/slog"
	"net"
	"regexp"
	"strings"
	"sync"
	"time"
	"unicode/utf8"
)

type Config struct {
	Listen          string
	MaxPlayers      int // per-room ceiling the host may choose up to
	DefaultPlayers  int
	MinBuild        string // empty = accept any build string, else exact match required
	Timing          Timing
	ConnTimeout     time.Duration
	PacketsPerSec   float64
	CodeTriesPerMin float64
	MaxRooms        int
}

type conn struct {
	cid      uint64
	addr     *net.UDPAddr
	username string
	token    string
	lastSeen time.Time
	lastSeq  uint32
	room     string
	bucket   bucket
}

type bucket struct {
	tokens float64
	last   time.Time
}

func (b *bucket) allow(rate, burst float64, now time.Time) bool {
	if b.last.IsZero() {
		b.tokens, b.last = burst, now
	}
	b.tokens += now.Sub(b.last).Seconds() * rate
	if b.tokens > burst {
		b.tokens = burst
	}
	b.last = now
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}

type Server struct {
	cfg      Config
	sock     *net.UDPConn
	mu       sync.Mutex
	conns    map[uint64]*conn
	byAddr   map[string]uint64
	rooms    map[string]*Room
	codes    map[string]string // code -> room id
	ipLimits map[string]*bucket
	codeTry  map[string]*bucket
	log      *slog.Logger
	now      func() time.Time
}

var usernameRe = regexp.MustCompile(`^[A-Za-z0-9_\-\. ]{3,16}$`)

// ValidUsername: 3-16 chars of letters, digits, space, _ - . ; no leading/trailing space,
// no control characters or markup (brief section 23).
func ValidUsername(s string) bool {
	return utf8.ValidString(s) && usernameRe.MatchString(s) && strings.TrimSpace(s) == s
}

var validModes = map[string]bool{"possession_deathmatch": true, "capture_the_host": true, "hunt": true}

func NewServer(cfg Config, log *slog.Logger) *Server {
	return &Server{cfg: cfg, conns: map[uint64]*conn{}, byAddr: map[string]uint64{}, rooms: map[string]*Room{},
		codes: map[string]string{}, ipLimits: map[string]*bucket{}, codeTry: map[string]*bucket{}, log: log, now: time.Now}
}

func (s *Server) Serve(sock *net.UDPConn) {
	s.sock = sock
	go s.tickLoop()
	buf := make([]byte, 2048)
	for {
		n, addr, err := sock.ReadFromUDP(buf)
		if err != nil {
			if ne, ok := err.(net.Error); ok && ne.Timeout() {
				continue
			}
			s.log.Info("socket closed", "err", err)
			return
		}
		s.handle(append([]byte(nil), buf[:n]...), addr)
	}
}

func (s *Server) tickLoop() {
	t := time.NewTicker(250 * time.Millisecond)
	for range t.C {
		s.tick()
	}
}

func header(kind byte, cid uint64, seq uint32) []byte {
	h := make([]byte, HeaderSize)
	copy(h, Magic)
	h[4] = kind
	binary.BigEndian.PutUint64(h[6:], cid)
	binary.BigEndian.PutUint32(h[14:], seq)
	return h
}

func (s *Server) sendCtl(addr *net.UDPAddr, cid uint64, c Control) {
	body, _ := json.Marshal(c)
	if s.sock != nil {
		s.sock.WriteToUDP(append(header(KindControl, cid, 0), body...), addr)
	}
}

func (s *Server) handle(pkt []byte, addr *net.UDPAddr) {
	now := s.now()
	s.mu.Lock()
	defer s.mu.Unlock()
	ip := addr.IP.String()
	lim := s.ipLimits[ip]
	if lim == nil {
		lim = &bucket{}
		s.ipLimits[ip] = lim
	}
	// Per-IP budget covers every connection from that address (several players behind one NAT).
	if !lim.allow(s.cfg.PacketsPerSec*4, s.cfg.PacketsPerSec*8, now) {
		return
	}
	if len(pkt) < HeaderSize || len(pkt) > MaxDatagram || string(pkt[:4]) != Magic {
		return // malformed: silently dropped
	}
	kind, cid, seq := pkt[4], binary.BigEndian.Uint64(pkt[6:14]), binary.BigEndian.Uint32(pkt[14:18])
	body := pkt[HeaderSize:]

	if cid == 0 {
		if kind == KindControl {
			var c Control
			if json.Unmarshal(body, &c) == nil && (c.Op == "HELLO" || c.Op == "REJOIN") {
				s.hello(addr, c, now)
			}
		}
		return
	}
	cn := s.conns[cid]
	if cn == nil || cn.addr.String() != addr.String() {
		return // unknown connection or spoofed source: drop
	}
	if !cn.bucket.allow(s.cfg.PacketsPerSec, s.cfg.PacketsPerSec*2, now) {
		return
	}
	cn.lastSeen = now
	switch kind {
	case KindControl:
		if seq <= cn.lastSeq {
			return // replayed or reordered control message
		}
		cn.lastSeq = seq
		var c Control
		if err := json.Unmarshal(body, &c); err != nil {
			s.sendCtl(addr, cid, Control{Op: "ERROR", Error: "malformed"})
			return
		}
		s.control(cn, c, now)
	case KindGame:
		s.routeGame(cn, body)
	}
}

func newID() uint64 {
	var b [8]byte
	for {
		if _, err := randRead(b[:]); err != nil {
			panic(err)
		}
		if v := binary.BigEndian.Uint64(b[:]); v != 0 {
			return v
		}
	}
}

func newToken() string {
	var b [16]byte
	randRead(b[:])
	return hex.EncodeToString(b[:])
}

func (s *Server) hello(addr *net.UDPAddr, c Control, now time.Time) {
	if c.Version != ProtocolVer {
		s.sendCtl(addr, 0, Control{Op: "ERROR", Error: "version_mismatch", Reason: "update GeistNative"})
		return
	}
	if s.cfg.MinBuild != "" && c.Build != s.cfg.MinBuild {
		s.sendCtl(addr, 0, Control{Op: "ERROR", Error: "build_mismatch"})
		return
	}
	if !ValidUsername(c.Username) {
		s.sendCtl(addr, 0, Control{Op: "ERROR", Error: "bad_username"})
		return
	}
	// One live connection per source address+port; a fresh HELLO replaces a stale one.
	if old, ok := s.byAddr[addr.String()]; ok {
		s.disconnect(old, "replaced", now)
	}
	cn := &conn{cid: newID(), addr: addr, username: c.Username, token: newToken(), lastSeen: now}
	s.conns[cn.cid] = cn
	s.byAddr[addr.String()] = cn.cid
	s.log.Info("connect", "cid", cn.cid, "user", cn.username)
	s.sendCtl(addr, cn.cid, Control{Op: "WELCOME", CID: hexID(cn.cid), ReconnectToken: cn.token, T: c.T})

	if c.Op == "REJOIN" && c.ReconnectToken != "" {
		for _, r := range s.rooms {
			if i, err := r.Reclaim(cn.cid, c.ReconnectToken, cn.username, s.cfg.Timing, now); err == nil {
				cn.room = r.ID
				slot := i
				s.log.Info("reclaim", "room", r.ID, "slot", i, "user", cn.username)
				s.sendCtl(addr, cn.cid, Control{Op: "JOINED", RoomID: r.ID, Slot: &slot, State: SlotHumanRejoining})
				s.notifyRoom(r, Control{Op: "SLOT", Slot: &slot, State: SlotHumanRejoining, Username: cn.username})
				return
			}
		}
	}
}

func hexID(v uint64) string {
	var b [8]byte
	binary.BigEndian.PutUint64(b[:], v)
	return hex.EncodeToString(b[:])
}

func (s *Server) control(cn *conn, c Control, now time.Time) {
	reply := func(r Control) { s.sendCtl(cn.addr, cn.cid, r) }
	switch c.Op {
	case "PING":
		reply(Control{Op: "PONG", T: c.T})
	case "HEARTBEAT":
	case "LIST":
		var out []RoomInfo
		for _, r := range s.rooms {
			if !r.Public || r.Status == StatusEnded {
				continue
			}
			ri := r.Info(s.hostName(r), false)
			if c.HideFull && ri.Joinable == 0 {
				continue
			}
			if c.Mode != "" && c.Mode != r.Mode || c.Map != "" && !strings.EqualFold(c.Map, r.Map) {
				continue
			}
			if c.Search != "" && !strings.Contains(strings.ToLower(ri.Host), strings.ToLower(c.Search)) {
				continue
			}
			out = append(out, ri)
			if len(out) >= 50 { // one datagram; clients page with filters
				break
			}
		}
		reply(Control{Op: "ROOMS", Rooms: out})
	case "HOST":
		if cn.room != "" {
			reply(Control{Op: "ERROR", Error: "already_in_room"})
			return
		}
		if len(s.rooms) >= s.cfg.MaxRooms {
			reply(Control{Op: "ERROR", Error: "server_full"})
			return
		}
		if !validModes[c.Mode] || c.Map == "" || len(c.Map) > 32 || len(c.Settings) > 32 {
			reply(Control{Op: "ERROR", Error: "bad_settings"})
			return
		}
		max := c.MaxPlayers
		if max == 0 {
			max = s.cfg.DefaultPlayers
		}
		if max < 2 || max > s.cfg.MaxPlayers {
			reply(Control{Op: "ERROR", Error: "bad_capacity"})
			return
		}
		// Capture the Host and Hunt are always two teams; Possession Deathmatch is
		// free-for-all unless the host turns teams on (team deathmatch).
		teams := c.Mode == "capture_the_host" || c.Mode == "hunt" || (c.Teams != nil && *c.Teams)
		if teams && max%2 != 0 {
			reply(Control{Op: "ERROR", Error: "bad_capacity", Reason: "team rooms need an even size"})
			return
		}
		public := c.Public == nil || *c.Public
		id := hexID(newID())
		r := NewRoom(id, public, cn.cid, cn.username, c.Mode, c.Map, max, c.Settings, teams)
		if !public {
			for {
				r.Code = NewRoomCode()
				if _, taken := s.codes[r.Code]; !taken {
					break
				}
			}
			s.codes[r.Code] = id
		}
		s.rooms[id] = r
		cn.room = id
		zero := 0
		info := r.Info(cn.username, true)
		s.log.Info("room_created", "room", id, "public", public, "mode", c.Mode, "map", c.Map, "max", max)
		reply(Control{Op: "HOSTED", RoomID: id, Code: r.Code, Slot: &zero, Room: &info})
	case "JOIN", "JOIN_CODE":
		if cn.room != "" {
			reply(Control{Op: "ERROR", Error: "already_in_room"})
			return
		}
		var r *Room
		if c.Op == "JOIN_CODE" {
			ip := cn.addr.IP.String()
			b := s.codeTry[ip]
			if b == nil {
				b = &bucket{}
				s.codeTry[ip] = b
			}
			if !b.allow(s.cfg.CodeTriesPerMin/60, 5, now) {
				reply(Control{Op: "ERROR", Error: "slow_down"})
				return
			}
			r = s.rooms[s.codes[strings.ToUpper(strings.TrimSpace(c.Code))]]
		} else if rr := s.rooms[c.RoomID]; rr != nil && rr.Public {
			r = rr
		}
		if r == nil || r.Status == StatusEnded {
			reply(Control{Op: "ERROR", Error: "no_such_room"})
			return
		}
		i, st, err := r.Admit(cn.cid, cn.username, c.Team, s.cfg.Timing, now)
		if err != nil {
			reply(Control{Op: "ERROR", Error: err.Error()})
			return
		}
		cn.room = r.ID
		info := r.Info(s.hostName(r), true)
		s.log.Info("join", "room", r.ID, "slot", i, "state", st, "user", cn.username)
		reply(Control{Op: "JOINED", RoomID: r.ID, Slot: &i, State: st, Room: &info, ReconnectToken: cn.token})
		s.notifyRoom(r, Control{Op: "SLOT", Slot: &i, State: st, Username: cn.username})
	case "SWITCH_TEAM":
		r := s.rooms[cn.room]
		if r == nil {
			reply(Control{Op: "ERROR", Error: "not_in_room"})
			return
		}
		i, err := r.SwitchTeam(cn.cid)
		if err != nil {
			reply(Control{Op: "ERROR", Error: err.Error()})
			return
		}
		info := r.Info(s.hostName(r), true)
		s.notifyRoom(r, Control{Op: "ROOM", Room: &info, Status: r.Status})
		_ = i
	case "LEAVE":
		s.leaveRoom(cn, "left", now)
		reply(Control{Op: "LEFT"})
	case "START", "END", "HANDOFF_DONE", "UPDATE":
		r := s.rooms[cn.room]
		if r == nil || r.HostCID != cn.cid {
			reply(Control{Op: "ERROR", Error: "not_host"})
			return
		}
		switch c.Op {
		case "START":
			r.Start()
		case "END":
			r.Status = StatusEnded
		case "UPDATE": // map / mode / settings changes between rounds
			if c.Map != "" {
				r.Map = c.Map
			}
			if validModes[c.Mode] {
				r.Mode = c.Mode
			}
			if c.Settings != nil && len(c.Settings) <= 32 {
				r.Settings = c.Settings
			}
		case "HANDOFF_DONE":
			if c.Slot == nil || r.ConfirmHandoff(*c.Slot) != nil {
				reply(Control{Op: "ERROR", Error: "bad_handoff"})
				return
			}
			s.log.Info("handoff", "room", r.ID, "slot", *c.Slot, "user", r.Slots[*c.Slot].Username)
			s.notifyRoom(r, Control{Op: "SLOT", Slot: c.Slot, State: SlotHumanActive, Username: r.Slots[*c.Slot].Username})
			return
		}
		info := r.Info(s.hostName(r), true)
		s.notifyRoom(r, Control{Op: "ROOM", Room: &info, Status: r.Status})
	case "ROOM_INFO":
		if r := s.rooms[cn.room]; r != nil {
			info := r.Info(s.hostName(r), true)
			reply(Control{Op: "ROOM", Room: &info})
		}
	default:
		reply(Control{Op: "ERROR", Error: "unknown_op"})
	}
}

func (s *Server) hostName(r *Room) string {
	if h := s.conns[r.HostCID]; h != nil {
		return h.username
	}
	return ""
}

func (s *Server) notifyRoom(r *Room, c Control) {
	for _, sl := range r.Slots {
		if cn := s.conns[sl.Owner]; cn != nil {
			s.sendCtl(cn.addr, cn.cid, c)
		}
	}
}

// routeGame forwards opaque game data. Clients reach only the host; the host reaches one
// slot or all. Packets from a slot that does not hold control yet are still routed to the
// host (the joiner must ack snapshots), but the host decides what to trust.
func (s *Server) routeGame(cn *conn, body []byte) {
	r := s.rooms[cn.room]
	if r == nil || len(body) < 1 || r.Status == StatusEnded {
		return
	}
	from := r.SlotOf(cn.cid)
	if from < 0 {
		return
	}
	target := body[0]
	// Rewrite the target byte as the sender slot so receivers learn who sent it.
	out := append(header(KindGame, 0, 0), byte(from))
	out = append(out, body[1:]...)
	send := func(cid uint64) {
		if d := s.conns[cid]; d != nil && s.sock != nil {
			s.sock.WriteToUDP(out, d.addr)
		}
	}
	if cn.cid != r.HostCID {
		if target == TargetHost {
			send(r.HostCID)
		}
		return
	}
	if target == TargetAll {
		for _, sl := range r.Slots {
			if sl.Owner != 0 && sl.Owner != cn.cid {
				send(sl.Owner)
			}
		}
	} else if int(target) < len(r.Slots) {
		send(r.Slots[target].Owner)
	}
}

func (s *Server) leaveRoom(cn *conn, why string, now time.Time) {
	r := s.rooms[cn.room]
	cn.room = ""
	if r == nil {
		return
	}
	if r.HostCID == cn.cid {
		// No host migration yet (brief section 62): end the session cleanly for everyone.
		s.log.Info("room_closed", "room", r.ID, "reason", "host_"+why)
		s.notifyRoom(r, Control{Op: "CLOSED", RoomID: r.ID, Reason: "host_" + why})
		for _, sl := range r.Slots {
			if c := s.conns[sl.Owner]; c != nil {
				c.room = ""
			}
		}
		delete(s.codes, r.Code)
		delete(s.rooms, r.ID)
		return
	}
	token := ""
	if why == "timeout" {
		token = cn.token // only an involuntary drop keeps a reconnect reservation
	}
	i, st := r.Drop(cn.cid, token, s.cfg.Timing, now)
	if i >= 0 {
		s.log.Info("slot_drop", "room", r.ID, "slot", i, "state", st, "user", cn.username, "why", why)
		s.notifyRoom(r, Control{Op: "SLOT", Slot: &i, State: st, Username: cn.username, Reason: why})
	}
}

func (s *Server) disconnect(cid uint64, why string, now time.Time) {
	cn := s.conns[cid]
	if cn == nil {
		return
	}
	s.leaveRoom(cn, why, now)
	delete(s.byAddr, cn.addr.String())
	delete(s.conns, cid)
	s.log.Info("disconnect", "cid", cid, "user", cn.username, "why", why)
}

func (s *Server) tick() {
	now := s.now()
	s.mu.Lock()
	defer s.mu.Unlock()
	for cid, cn := range s.conns {
		if now.Sub(cn.lastSeen) > s.cfg.ConnTimeout {
			s.disconnect(cid, "timeout", now)
		}
	}
	for _, r := range s.rooms {
		for _, i := range r.Tick(now) {
			i := i
			sl := r.Slots[i]
			s.log.Info("slot_expire", "room", r.ID, "slot", i, "state", sl.State)
			s.notifyRoom(r, Control{Op: "SLOT", Slot: &i, State: sl.State, Reason: "expired"})
		}
		// A joiner whose handoff expired no longer owns anything; detach them.
		for _, cn := range s.conns {
			if cn.room == r.ID && r.SlotOf(cn.cid) < 0 && r.HostCID != cn.cid {
				cn.room = ""
				s.sendCtl(cn.addr, cn.cid, Control{Op: "ERROR", Error: "handoff_timeout"})
			}
		}
	}
	for ip, b := range s.ipLimits {
		if now.Sub(b.last) > time.Minute {
			delete(s.ipLimits, ip)
		}
	}
}
