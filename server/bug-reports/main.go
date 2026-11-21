package main

import (
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"io"
	"log"
	"mime/multipart"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unicode/utf8"
)

const maxUpload = 8 << 20
const maxRequest = maxUpload + (128 << 10)

var categories = map[string]bool{
	"Game crash": true, "Graphics problem": true, "Widescreen issue": true,
	"Mouse or keyboard problem": true, "Controller problem": true, "Audio problem": true,
	"Performance problem": true, "Save file problem": true, "Launcher issue": true, "Other": true,
}

type report struct {
	Version      string `json:"version"`
	Category     string `json:"category"`
	Description  string `json:"description"`
	Activity     string `json:"activity"`
	Steps        string `json:"steps"`
	Expected     string `json:"expected"`
	Actual       string `json:"actual"`
	Comments     string `json:"comments"`
	EvidenceLink string `json:"evidence_link"`
	Hardware     string `json:"hardware"`
	Website      string `json:"website"`
}
type attachment struct {
	Name string
	Data []byte
}
type bucket struct {
	Count int
	Start time.Time
}
type service struct {
	webhook string
	proxy   *net.IPNet
	client  *http.Client
	mu      sync.Mutex
	clients map[string]bucket
	global  bucket
	busy    chan struct{}
}

func respond(w http.ResponseWriter, code int, body any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(body)
}
func fail(w http.ResponseWriter, code int, message string) {
	respond(w, code, map[string]string{"error": message})
}
func (s *service) allow(ip string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	// Expire entries so arbitrary source addresses cannot grow this map indefinitely.
	for key, entry := range s.clients {
		if now.Sub(entry.Start) > time.Hour {
			delete(s.clients, key)
		}
	}
	if now.Sub(s.global.Start) > time.Hour {
		s.global = bucket{Start: now}
	}
	entry := s.clients[ip]
	if now.Sub(entry.Start) > time.Hour {
		entry = bucket{Start: now}
	}
	if entry.Count >= 5 || s.global.Count >= 100 || len(s.clients) >= 10000 {
		return false
	}
	entry.Count++
	s.clients[ip] = entry
	s.global.Count++
	return true
}
func validReport(r report) error {
	required := []string{r.Version, r.Description, r.Activity, r.Steps, r.Expected, r.Actual}
	for _, value := range required {
		if strings.TrimSpace(value) == "" {
			return errors.New("Complete all required report fields.")
		}
	}
	if !categories[r.Category] {
		return errors.New("Choose a valid report category.")
	}
	if r.Website != "" {
		return errors.New("The report could not be accepted.")
	}
	if len(r.Version) > 32 {
		return errors.New("The version must be at most 32 characters.")
	}
	for _, value := range []string{r.Description, r.Activity, r.Steps, r.Expected, r.Actual, r.Comments, r.Hardware} {
		if !utf8.ValidString(value) || len(value) > 4000 {
			return errors.New("Each text field must be valid UTF-8 and at most 4000 bytes.")
		}
	}
	if r.EvidenceLink != "" {
		link, err := url.Parse(r.EvidenceLink)
		if err != nil || link.Scheme != "https" || link.Hostname() == "" ||
			link.User != nil || len(r.EvidenceLink) > 1000 {
			return errors.New("Use a public HTTPS link for large videos.")
		}
	}
	return nil
}
func validAttachment(name string, data []byte) bool {
	if strings.ContainsAny(name, "\r\n") {
		return false
	}
	extension := strings.ToLower(filepath.Ext(name))
	if extension == ".mp4" {
		return len(data) >= 16 && string(data[4:8]) == "ftyp"
	}
	if extension != ".png" && extension != ".jpg" && extension != ".jpeg" {
		return false
	}
	config, format, err := image.DecodeConfig(bytes.NewReader(data))
	if err != nil || config.Width < 1 || config.Height < 1 || config.Width > 16384 || config.Height > 16384 {
		return false
	}
	return (extension == ".png" && format == "png") || (extension != ".png" && format == "jpeg")
}
func parseReport(w http.ResponseWriter, req *http.Request) (report, []attachment, error) {
	req.Body = http.MaxBytesReader(w, req.Body, maxRequest)
	reader, err := req.MultipartReader()
	if err != nil {
		return report{}, nil, errors.New("Send a multipart report with PNG, JPEG or MP4 evidence.")
	}
	var r report
	var files []attachment
	seen := false
	used := 0
	for {
		part, err := reader.NextPart()
		if err == io.EOF {
			break
		}
		if err != nil {
			return r, nil, errors.New("The upload is incomplete or exceeds the 8 MiB evidence limit.")
		}
		if part.FormName() == "report" && part.FileName() == "" && !seen {
			data, err := io.ReadAll(io.LimitReader(part, 32769))
			if err != nil || len(data) > 32768 {
				return r, nil, errors.New("The report text is too large.")
			}
			decoder := json.NewDecoder(bytes.NewReader(data))
			decoder.DisallowUnknownFields()
			if decoder.Decode(&r) != nil || decoder.Decode(new(any)) != io.EOF {
				return r, nil, errors.New("The report fields could not be read.")
			}
			seen = true
		} else if part.FormName() == "file" && part.FileName() != "" {
			if len(files) >= 4 {
				return r, nil, errors.New("Attach at most four evidence files.")
			}
			data, err := io.ReadAll(io.LimitReader(part, int64(maxUpload-used+1)))
			used += len(data)
			if err != nil || used > maxUpload {
				return r, nil, errors.New("Attachments must total at most 8 MiB. Add an HTTPS sharing link for a larger video.")
			}
			name := filepath.Base(strings.ReplaceAll(part.FileName(), "\\", "/"))
			if len(name) > 160 || !validAttachment(name, data) {
				return r, nil, errors.New("An attachment has an unsupported name or file content. Choose a PNG, JPEG or MP4 file.")
			}
			files = append(files, attachment{Name: name, Data: data})
		} else {
			return r, nil, errors.New("The report contains an unsupported or duplicate field.")
		}
		part.Close()
	}
	if !seen {
		return r, nil, errors.New("Report details are missing.")
	}
	return r, files, validReport(r)
}
func reportText(id string, r report, when time.Time) string {
	return fmt.Sprintf(`GEIST RECOMPILED BUG REPORT
Report ID: %s
Version: %s
Category: %s
Date: %s

What happened?
%s

What were you doing?
%s

Steps to reproduce
%s

Expected behavior
%s

Actual behavior
%s

Additional comments
%s

Evidence link
%s

Optional hardware information
%s
`,
		id, r.Version, r.Category, when.Format(time.RFC3339), r.Description, r.Activity,
		r.Steps, r.Expected, r.Actual, r.Comments, r.EvidenceLink, r.Hardware)
}
func (s *service) deliver(id string, r report, files []attachment) error {
	now := time.Now().UTC()
	// The full text is always attached so long fields are delivered without truncation.
	text := attachment{Name: "report-" + id + ".txt", Data: []byte(reportText(id, r, now))}
	all := append([]attachment{text}, files...)
	var body bytes.Buffer
	writer := multipart.NewWriter(&body)
	description := r.Description
	if len([]rune(description)) > 1500 {
		description = string([]rune(description)[:1500]) + "\nFull details are in the attached report."
	}
	payload := map[string]any{
		"username":         "Geist Recompiled Bug Reports",
		"allowed_mentions": map[string]any{"parse": []string{}},
		"embeds": []any{map[string]any{
			"title":       "Geist Recompiled | " + r.Category,
			"description": description,
			"color":       3652863,
			"timestamp":   now.Format(time.RFC3339),
			"fields": []any{
				map[string]any{"name": "Report ID", "value": id, "inline": true},
				map[string]any{"name": "Version", "value": r.Version, "inline": true},
				map[string]any{"name": "Evidence files", "value": fmt.Sprint(len(files)), "inline": true},
			},
		}},
	}
	data, err := json.Marshal(payload)
	if err != nil {
		return errors.New("The report could not be prepared.")
	}
	field, err := writer.CreateFormField("payload_json")
	if err != nil {
		return err
	}
	field.Write(data)
	for i, file := range all {
		field, err := writer.CreateFormFile(fmt.Sprintf("files[%d]", i), file.Name)
		if err != nil {
			return err
		}
		if _, err = field.Write(file.Data); err != nil {
			return err
		}
	}
	if err := writer.Close(); err != nil {
		return err
	}
	endpoint, _ := url.Parse(s.webhook)
	query := endpoint.Query()
	query.Set("wait", "true")
	endpoint.RawQuery = query.Encode()
	req, err := http.NewRequest(http.MethodPost, endpoint.String(), &body)
	if err != nil {
		return errors.New("The report service is not configured correctly.")
	}
	req.Header.Set("Content-Type", writer.FormDataContentType())
	res, err := s.client.Do(req)
	if err != nil {
		return errors.New("Delivery could not be confirmed. Keep your report and retry. A timed-out request may already have reached the team.")
	}
	defer res.Body.Close()
	if res.StatusCode == http.StatusTooManyRequests {
		return errors.New("Discord is busy. Wait a minute and retry your report.")
	}
	if res.StatusCode == http.StatusRequestEntityTooLarge {
		return errors.New("Discord rejected the attachment size. Use smaller files or an HTTPS video link.")
	}
	if res.StatusCode < 200 || res.StatusCode >= 300 {
		return errors.New("Discord could not accept this report. Keep your details and try again later.")
	}
	var message struct {
		ID          string            `json:"id"`
		Attachments []json.RawMessage `json:"attachments"`
	}
	if json.NewDecoder(io.LimitReader(res.Body, 1<<20)).Decode(&message) != nil ||
		message.ID == "" || len(message.Attachments) != len(all) {
		return errors.New("The report or its attachments could not be confirmed. Keep your report and retry.")
	}
	log.Printf("report=%s delivered evidence_files=%d", id, len(files))
	return nil
}
func (s *service) ServeHTTP(w http.ResponseWriter, req *http.Request) {
	if req.URL.Path == "/healthz" && req.Method == http.MethodGet {
		respond(w, 200, map[string]string{"status": "ok"})
		return
	}
	if req.URL.Path != "/v1/reports" {
		fail(w, 404, "Not found.")
		return
	}
	if req.Method != http.MethodPost {
		w.Header().Set("Allow", "POST")
		fail(w, 405, "Use Submit Report in Geist Recompiled.")
		return
	}
	ip, _, err := net.SplitHostPort(req.RemoteAddr)
	if err != nil {
		fail(w, 400, "The connection could not be verified.")
		return
	}
	// Only the configured private proxy may supply the original client address.
	if s.proxy != nil && s.proxy.Contains(net.ParseIP(ip)) {
		forwarded := net.ParseIP(req.Header.Get("X-Forwarded-For"))
		if forwarded == nil {
			fail(w, 400, "The proxy did not provide a client address.")
			return
		}
		ip = forwarded.String()
	}
	if !s.allow(ip) {
		fail(w, 429, "Too many reports. Please try again in one hour.")
		return
	}
	select {
	case s.busy <- struct{}{}:
		defer func() { <-s.busy }()
	default:
		fail(w, 503, "The report service is busy. Please retry shortly.")
		return
	}
	r, files, err := parseReport(w, req)
	if err != nil {
		fail(w, 400, err.Error())
		return
	}
	idBytes := make([]byte, 6)
	if _, err := rand.Read(idBytes); err != nil {
		fail(w, 503, "The report service could not assign an ID. Please retry.")
		return
	}
	id := strings.ToUpper(hex.EncodeToString(idBytes))
	if err := s.deliver(id, r, files); err != nil {
		fail(w, 502, err.Error())
		return
	}
	respond(w, 201, map[string]any{"report_id": id, "attachments": len(files), "delivered": true})
}
func main() {
	webhook := os.Getenv("GEIST_DISCORD_WEBHOOK")
	target, err := url.Parse(webhook)
	if err != nil || target.Scheme != "https" || target.Hostname() != "discord.com" ||
		!strings.HasPrefix(target.Path, "/api/webhooks/") || target.User != nil {
		log.Fatal("Set GEIST_DISCORD_WEBHOOK to the Discord webhook on the server.")
	}
	var proxy *net.IPNet
	if cidr := os.Getenv("GEIST_TRUSTED_PROXY"); cidr != "" {
		_, proxy, err = net.ParseCIDR(cidr)
		if err != nil {
			log.Fatal("GEIST_TRUSTED_PROXY must be a private proxy CIDR.")
		}
	}
	listen := os.Getenv("GEIST_REPORT_LISTEN")
	if listen == "" {
		listen = "127.0.0.1:8080"
	}
	s := &service{
		webhook: webhook, proxy: proxy, clients: make(map[string]bucket), busy: make(chan struct{}, 4),
		client: &http.Client{Timeout: 45 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }},
	}
	server := &http.Server{Addr: listen, Handler: s, ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout: 60 * time.Second, WriteTimeout: 60 * time.Second, IdleTimeout: 30 * time.Second,
		MaxHeaderBytes: 16 << 10}
	log.Printf("Geist report service listening on %s", listen)
	log.Fatal(server.ListenAndServe())
}
