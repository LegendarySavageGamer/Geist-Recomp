package main

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net"
	"sync"
	"testing"
	"time"
)

// ---- fake clock so grace periods can be tested without sleeping ----
type clock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *clock) Now() time.Time      { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *clock) Add(d time.Duration) { c.mu.Lock(); c.t = c.t.Add(d); c.mu.Unlock() }

type harness struct {
	t    *testing.T
	srv  *Server
	clk  *clock
	addr *net.UDPAddr
}

func newHarness(t *testing.T) *harness {
	clk := &clock{t: time.Unix(1_700_000_000, 0)}
	cfg := Config{MaxPlayers: 16, DefaultPlayers: 16,
		Timing:      Timing{ReconnectGrace: 45 * time.Second, HandoffTimeout: 20 * time.Second},
		ConnTimeout: 10 * time.Second, PacketsPerSec: 500, CodeTriesPerMin: 600, MaxRooms: 16}
	s := NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	s.now = clk.Now
	sock, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	s.sock = sock
	go func() { // Serve without the real-time tick loop; tests call tick() explicitly.
		buf := make([]byte, 2048)
		for {
			n, a, err := sock.ReadFromUDP(buf)
			if err != nil {
				return
			}
			s.handle(append([]byte(nil), buf[:n]...), a)
		}
	}()
	t.Cleanup(func() { sock.Close() })
	return &harness{t: t, srv: s, clk: clk, addr: sock.LocalAddr().(*net.UDPAddr)}
}

type client struct {
	h     *harness
	sock  *net.UDPConn
	cid   uint64
	seq   uint32
	token string
	name  string
}

func (h *harness) connect(name string) *client {
	sock, err := net.DialUDP("udp", nil, h.addr)
	if err != nil {
		h.t.Fatal(err)
	}
	h.t.Cleanup(func() { sock.Close() })
	c := &client{h: h, sock: sock, name: name}
	c.sendRaw(KindControl, 0, Control{Op: "HELLO", Version: ProtocolVer, Username: name})
	w := c.expect("WELCOME")
	c.token = w.ReconnectToken
	return c
}

func (c *client) sendRaw(kind byte, cid uint64, ctl Control) {
	c.seq++
	b, _ := json.Marshal(ctl)
	c.sock.Write(append(header(kind, cid, c.seq), b...))
}

func (c *client) send(ctl Control) { c.sendRaw(KindControl, c.cid, ctl) }

func (c *client) sendGame(target byte, data string) {
	c.sock.Write(append(append(header(KindGame, c.cid, 0), target), data...))
}

func (c *client) read(timeout time.Duration) (byte, []byte, bool) {
	buf := make([]byte, 2048)
	c.sock.SetReadDeadline(time.Now().Add(timeout))
	n, err := c.sock.Read(buf)
	if err != nil {
		return 0, nil, false
	}
	if c.cid == 0 && buf[4] == KindControl {
		c.cid = binary.BigEndian.Uint64(buf[6:14])
	}
	return buf[4], buf[HeaderSize:n], true
}

// expect reads until a control message with op arrives (skipping others).
func (c *client) expect(op string) Control {
	c.h.t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		k, body, ok := c.read(time.Until(deadline))
		if !ok {
			break
		}
		if k != KindControl {
			continue
		}
		var ctl Control
		json.Unmarshal(body, &ctl)
		if ctl.Op == op {
			return ctl
		}
	}
	c.h.t.Fatalf("%s: never got %s", c.name, op)
	return Control{}
}

func (c *client) expectGame() []byte {
	c.h.t.Helper()
	for i := 0; i < 20; i++ {
		k, body, ok := c.read(time.Second)
		if !ok {
			break
		}
		if k == KindGame {
			return body
		}
	}
	c.h.t.Fatalf("%s: no game packet", c.name)
	return nil
}

func (c *client) drain() {
	for {
		if _, _, ok := c.read(30 * time.Millisecond); !ok {
			return
		}
	}
}

func pub(b bool) *bool { return &b }

func (h *harness) list(c *client, f Control) []RoomInfo {
	f.Op = "LIST"
	c.send(f)
	return c.expect("ROOMS").Rooms
}

// N01-N02, N03, N06, N08-N10: relay up, clients connect, public room listed with real fields.
func TestPublicRoomListed(t *testing.T) {
	h := newHarness(t)
	host := h.connect("ThyHeroOfTime")
	host.send(Control{Op: "HOST", Public: pub(true), Mode: "possession_deathmatch", Map: "Z_OFFICE", MaxPlayers: 4})
	hosted := host.expect("HOSTED")
	if hosted.Code != "" {
		t.Fatal("public room must not get a code")
	}
	b := h.connect("Velvet_Charm")
	rooms := h.list(b, Control{})
	if len(rooms) != 1 || rooms[0].Host != "ThyHeroOfTime" || rooms[0].Map != "Z_OFFICE" ||
		rooms[0].Mode != "possession_deathmatch" || rooms[0].Humans != 1 || rooms[0].MaxPlayers != 4 {
		t.Fatalf("bad listing %+v", rooms)
	}
}

// N04, N05, N30: private room has a code, is hidden from LIST, and the code admits.
func TestPrivateRoomCode(t *testing.T) {
	h := newHarness(t)
	host := h.connect("Host1")
	host.send(Control{Op: "HOST", Public: pub(false), Mode: "hunt", Map: "HUNT_HEAVEN"})
	hosted := host.expect("HOSTED")
	code := hosted.Code
	if len(code) != len("GHOST-XXXXXX") {
		t.Fatalf("code %q", code)
	}
	friend := h.connect("Friend")
	if rooms := h.list(friend, Control{}); len(rooms) != 0 {
		t.Fatal("private room leaked into public list")
	}
	friend.send(Control{Op: "JOIN_CODE", Code: "GHOST-AAAAAA"})
	if e := friend.expect("ERROR"); e.Error != "no_such_room" {
		t.Fatal(e)
	}
	friend.send(Control{Op: "JOIN_CODE", Code: code})
	// Hunt is a team mode: host on team 1 (slot 0), the first joiner balances onto team 2.
	if j := friend.expect("JOINED"); *j.Slot != 8 {
		t.Fatalf("slot %v", *j.Slot)
	}
	// Joining a private room by id (bypassing the code) must fail.
	other := h.connect("Other")
	other.send(Control{Op: "JOIN", RoomID: hosted.RoomID})
	other.expect("ERROR")
}

// N07, N12-N14, N16 routing: 4 humans, counts update, clients only reach host, host fans out.
func TestFourPlayersAndRouting(t *testing.T) {
	h := newHarness(t)
	host := h.connect("Hostman")
	host.send(Control{Op: "HOST", Mode: "possession_deathmatch", Map: "Z_Base", MaxPlayers: 4})
	id := host.expect("HOSTED").RoomID
	var cs []*client
	for _, n := range []string{"P1x", "P2x", "P3x"} {
		c := h.connect(n)
		c.send(Control{Op: "JOIN", RoomID: id})
		c.expect("JOINED")
		cs = append(cs, c)
	}
	probe := h.connect("Probe")
	if r := h.list(probe, Control{}); r[0].Humans != 4 || r[0].Joinable != 0 {
		t.Fatalf("%+v", r[0])
	}
	if r := h.list(probe, Control{HideFull: true}); len(r) != 0 {
		t.Fatal("full room shown with hide_full")
	}
	probe.send(Control{Op: "JOIN", RoomID: id})
	if e := probe.expect("ERROR"); e.Error != "room_full" {
		t.Fatal(e)
	}
	host.drain()
	cs[0].sendGame(TargetAll, "client-broadcast-attempt") // must NOT reach other clients
	cs[0].sendGame(TargetHost, "input")
	if g := host.expectGame(); g[0] != 1 || string(g[1:]) != "input" {
		t.Fatalf("host got %q", g)
	}
	cs[1].drain()
	if _, _, ok := cs[1].read(100 * time.Millisecond); ok {
		t.Fatal("client-to-client traffic leaked")
	}
	host.sendGame(TargetAll, "snapshot")
	for _, c := range cs {
		if g := c.expectGame(); g[0] != 0 || string(g[1:]) != "snapshot" {
			t.Fatalf("client got %q", g)
		}
	}
}

// N23-N29 and the brief's section 61 scenario, end to end.
func TestDisconnectBotTakeoverAndReplacement(t *testing.T) {
	h := newHarness(t)
	host := h.connect("ThyHeroOfTime")
	host.send(Control{Op: "HOST", Mode: "possession_deathmatch", Map: "Z_Arena", MaxPlayers: 4})
	id := host.expect("HOSTED").RoomID
	ps := []*client{}
	for _, n := range []string{"Velvet_Charm", "Sanic", "Kit"} {
		c := h.connect(n)
		c.send(Control{Op: "JOIN", RoomID: id})
		c.expect("JOINED")
		ps = append(ps, c)
	}
	host.send(Control{Op: "START"})
	host.expect("ROOM")
	host.drain()

	// Player in slot 3 (Kit) stops sending: N23 detection after ConnTimeout.
	h.clk.Add(11 * time.Second)
	for _, c := range []*client{host, ps[0], ps[1]} { // live clients heartbeat
		c.send(Control{Op: "HEARTBEAT"})
	}
	time.Sleep(50 * time.Millisecond)
	h.srv.mu.Lock()
	h.srv.lastSeenFix(host, ps[0], ps[1])
	h.srv.mu.Unlock()
	h.srv.tick()
	ev := host.expect("SLOT")
	if *ev.Slot != 3 || ev.State != SlotBotTakeover {
		t.Fatalf("N24: want slot 3 BOT_TAKEOVER, got %+v", ev)
	}
	probe := h.connect("Newcomer")
	r := h.list(probe, Control{})[0]
	if r.Humans != 3 || r.Bots != 1 || r.Joinable != 0 {
		t.Fatalf("during grace the slot is reserved for Kit: %+v", r)
	}
	probe.send(Control{Op: "JOIN", RoomID: id})
	probe.expect("ERROR")

	// N25: grace expires -> slot opens to the public.
	h.clk.Add(46 * time.Second)
	h.srv.mu.Lock()
	h.srv.lastSeenFix(host, ps[0], ps[1], probe)
	h.srv.mu.Unlock()
	h.srv.tick()
	r = h.list(probe, Control{})[0]
	if r.Humans != 3 || r.Bots != 1 || r.Joinable != 1 || r.Status != StatusInProgress {
		t.Fatalf("N25: %+v", r)
	}
	// N26: new human joins the running match into the bot slot, pending handoff.
	probe.send(Control{Op: "JOIN", RoomID: id})
	j := probe.expect("JOINED")
	if *j.Slot != 3 || j.State != SlotHumanRejoining {
		t.Fatalf("N26: %+v", j)
	}
	// Kit comes back too late: reconnect token no longer valid, cannot double-claim (N29).
	late := h.connect("Kit")
	late.send(Control{Op: "JOIN", RoomID: id})
	if e := late.expect("ERROR"); e.Error != "room_full" {
		t.Fatalf("N28/N29: late rejoin should not get a slot: %+v", e)
	}
	// Only the host may confirm handoff.
	ps[0].send(Control{Op: "HANDOFF_DONE", Slot: j.Slot})
	ps[0].expect("ERROR")
	host.send(Control{Op: "HANDOFF_DONE", Slot: j.Slot})
	ev = probe.expect("SLOT")
	if ev.State == SlotHumanRejoining { // our own join notice arrives first
		ev = probe.expect("SLOT")
	}
	if ev.State != SlotHumanActive || ev.Username != "Newcomer" {
		t.Fatalf("N27: %+v", ev)
	}
	r = h.list(late, Control{})[0]
	if r.Humans != 4 || r.Bots != 0 {
		t.Fatalf("N28: no leftover bot: %+v", r)
	}
}

// N29: reconnect within grace reclaims the same slot with the token.
func TestReconnectWithinGrace(t *testing.T) {
	h := newHarness(t)
	host := h.connect("Hosty")
	host.send(Control{Op: "HOST", Mode: "hunt", Map: "HUNT_ARENA"})
	id := host.expect("HOSTED").RoomID
	p := h.connect("Dropper")
	p.send(Control{Op: "JOIN", RoomID: id})
	jd := p.expect("JOINED")
	tok, mySlot := jd.ReconnectToken, *jd.Slot
	host.send(Control{Op: "START"})
	host.expect("ROOM") // START processed before the clock moves
	h.clk.Add(11 * time.Second)
	h.srv.mu.Lock()
	h.srv.lastSeenFix(host)
	h.srv.mu.Unlock()
	h.srv.tick()
	host.expect("SLOT")
	// New socket (new port), same token.
	sock, _ := net.DialUDP("udp", nil, h.addr)
	defer sock.Close()
	re := &client{h: h, sock: sock, name: "Dropper"}
	re.sendRaw(KindControl, 0, Control{Op: "REJOIN", Version: ProtocolVer, Username: "Dropper", ReconnectToken: tok})
	re.expect("WELCOME")
	if j := re.expect("JOINED"); *j.Slot != mySlot || j.State != SlotHumanRejoining {
		t.Fatalf("%+v", j)
	}
	// A second use of the same token cannot claim anything.
	sock2, _ := net.DialUDP("udp", nil, h.addr)
	defer sock2.Close()
	dup := &client{h: h, sock: sock2, name: "Thief"}
	dup.sendRaw(KindControl, 0, Control{Op: "REJOIN", Version: ProtocolVer, Username: "Thief", ReconnectToken: tok})
	dup.expect("WELCOME")
	if _, body, ok := dup.read(150 * time.Millisecond); ok && string(body) != "" {
		var c Control
		json.Unmarshal(body, &c)
		if c.Op == "JOINED" {
			t.Fatal("token reused")
		}
	}
}

// Section 62: host leaving ends the session for everyone (no silent migration).
func TestHostLeaveClosesRoom(t *testing.T) {
	h := newHarness(t)
	host := h.connect("Hoster")
	host.send(Control{Op: "HOST", Mode: "hunt", Map: "HUNT_RAID"})
	id := host.expect("HOSTED").RoomID
	p := h.connect("Guest")
	p.send(Control{Op: "JOIN", RoomID: id})
	p.expect("JOINED")
	host.send(Control{Op: "LEAVE"})
	if c := p.expect("CLOSED"); c.Reason != "host_left" {
		t.Fatal(c)
	}
	if len(h.list(p, Control{})) != 0 {
		t.Fatal("stale room listed")
	}
}

// Security basics: usernames, malformed packets, replayed control messages, spoofed cid.
func TestValidationAndReplay(t *testing.T) {
	for _, bad := range []string{"ab", "<script>", "a\x00bcd", " lead", "waytoolongusername123", "Ünï"} {
		if ValidUsername(bad) {
			t.Errorf("accepted %q", bad)
		}
	}
	for _, ok := range []string{"ThyHeroOfTime", "Velvet_Charm", "Sanic", "Kit"} {
		if !ValidUsername(ok) {
			t.Errorf("rejected %q", ok)
		}
	}
	h := newHarness(t)
	c := h.connect("Replayer")
	c.sock.Write([]byte("garbage"))
	c.send(Control{Op: "PING", T: 42})
	if p := c.expect("PONG"); p.T != 42 {
		t.Fatal(p)
	}
	c.seq-- // replay the same seq
	c.send(Control{Op: "PING", T: 43})
	if _, _, ok := c.read(150 * time.Millisecond); ok {
		t.Fatal("replayed control message was answered")
	}
	// Another socket claiming our cid is ignored.
	sock, _ := net.DialUDP("udp", nil, h.addr)
	defer sock.Close()
	spoof := &client{h: h, sock: sock, cid: c.cid, name: "spoof"}
	spoof.send(Control{Op: "PING", T: 1})
	if _, _, ok := spoof.read(150 * time.Millisecond); ok {
		t.Fatal("spoofed cid answered")
	}
}

func TestNoBotFillerOnStart(t *testing.T) {
	r := NewRoom("x", true, 1, "H", "possession_deathmatch", "Z_Arena", 4, nil, false)
	r.Admit(2, "B", 0, Timing{}, time.Now())
	r.Start()
	_, bots, _ := r.Counts()
	if bots != 0 || r.Slots[2].State != SlotEmpty || r.Slots[3].State != SlotEmpty {
		t.Fatal("empty slots must stay empty at match start")
	}
}

// lastSeenFix marks live test clients as heard from at the fake clock's time (stands in
// for the heartbeats a real client sends every second). Caller holds s.mu.
func (s *Server) lastSeenFix(cs ...*client) {
	for _, c := range cs {
		if cn := s.conns[c.cid]; cn != nil {
			cn.lastSeen = s.now()
		}
	}
}

// 16 humans in free-for-all; the 17th is refused.
func TestSixteenFreeForAll(t *testing.T) {
	h := newHarness(t)
	host := h.connect("Host16")
	host.send(Control{Op: "HOST", Mode: "possession_deathmatch", Map: "Z_Complex"})
	hosted := host.expect("HOSTED")
	if hosted.Room.MaxPlayers != 16 || hosted.Room.Teams {
		t.Fatalf("default room should be 16 free-for-all: %+v", hosted.Room)
	}
	for i := 1; i < 16; i++ {
		c := h.connect(fmt.Sprintf("Player%02d", i))
		c.send(Control{Op: "JOIN", RoomID: hosted.RoomID})
		if j := c.expect("JOINED"); *j.Slot != i {
			t.Fatalf("player %d got slot %d", i, *j.Slot)
		}
	}
	extra := h.connect("Player17")
	if r := h.list(extra, Control{}); r[0].Humans != 16 || r[0].Joinable != 0 {
		t.Fatalf("%+v", r[0])
	}
	extra.send(Control{Op: "JOIN", RoomID: hosted.RoomID})
	if e := extra.expect("ERROR"); e.Error != "room_full" {
		t.Fatal(e)
	}
}

// 8 vs 8: joiners alternate onto the emptier team, a requested team is honoured while it
// has room, nobody gets a 9th seat on a team, and the 17th player is refused.
func TestEightVersusEight(t *testing.T) {
	h := newHarness(t)
	host := h.connect("TeamHost")
	host.send(Control{Op: "HOST", Mode: "capture_the_host", Map: "Z_Base"})
	hosted := host.expect("HOSTED")
	if !hosted.Room.Teams || hosted.Room.MaxPlayers != 16 {
		t.Fatalf("CTH must be a 16-slot team room: %+v", hosted.Room)
	}
	// Ask for team 1 eight times: 7 fit beside the host, the 8th is moved to team 2.
	got := map[int]int{}
	for i := 0; i < 8; i++ {
		c := h.connect(fmt.Sprintf("Red%d", i))
		c.send(Control{Op: "JOIN", RoomID: hosted.RoomID, Team: 1})
		j := c.expect("JOINED")
		got[teamOfSlot(*j.Slot)]++
	}
	if got[1] != 7 || got[2] != 1 {
		t.Fatalf("team 1 should cap at 8 incl. host: %v", got)
	}
	for i := 0; i < 7; i++ {
		c := h.connect(fmt.Sprintf("Blue%d", i))
		c.send(Control{Op: "JOIN", RoomID: hosted.RoomID})
		if s := *c.expect("JOINED").Slot; teamOfSlot(s) != 2 {
			t.Fatalf("blue joiner landed on team 1 slot %d", s)
		}
	}
	probe := h.connect("Spectator")
	r := h.list(probe, Control{})[0]
	if r.Humans != 16 || len(r.TeamFill) != 2 || r.TeamFill[0] != 8 || r.TeamFill[1] != 8 {
		t.Fatalf("want 8 vs 8: %+v", r)
	}
	probe.send(Control{Op: "JOIN", RoomID: hosted.RoomID})
	if e := probe.expect("ERROR"); e.Error != "room_full" {
		t.Fatal(e)
	}
}

func teamOfSlot(s int) int {
	if s < 8 {
		return 1
	}
	return 2
}

func TestSwitchTeamAndBotKeepsTeam(t *testing.T) {
	tm := Timing{ReconnectGrace: time.Second, HandoffTimeout: time.Second}
	now := time.Unix(0, 0)
	r := NewRoom("x", true, 1, "Host", "hunt", "HUNT_ARENA", 16, nil, true)
	i, _, _ := r.Admit(2, "Ghosty", 0, tm, now)
	if r.TeamOf(i) != 2 {
		t.Fatal("first joiner should balance to team 2")
	}
	j, err := r.SwitchTeam(2)
	if err != nil || r.TeamOf(j) != 1 {
		t.Fatalf("switch to team 1: slot %d err %v", j, err)
	}
	r.Start()
	if _, err := r.SwitchTeam(2); err == nil {
		t.Fatal("team switch must be refused mid-match")
	}
	// Ghosty drops: the replacement bot sits in the same slot, on the same team.
	r.Drop(2, "tok", tm, now)
	if r.Slots[j].State != SlotBotTakeover || r.TeamOf(j) != 1 {
		t.Fatal("bot must take the slot on the same team")
	}
	// After grace a newcomer asking for team 2 still gets the team 1 bot slot only if
	// team 2 has no free slot; team 2 has 8 empty slots, so they go there instead.
	r.Tick(now.Add(2 * time.Second))
	k, st, _ := r.Admit(3, "Newbie", 2, tm, now.Add(2*time.Second))
	if r.TeamOf(k) != 2 || st != SlotHumanRejoining {
		t.Fatalf("newcomer slot %d state %s", k, st)
	}
}
