package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"io"
	"mime/multipart"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func fixture() report {
	return report{Version: "1.1.1", Category: "Graphics problem", Description: "The model changes size.",
		Activity: "Walking next to a wall.", Steps: "Walk forward and turn.", Expected: "Stable size.", Actual: "The model grows."}
}
func pngFile() []byte {
	var data bytes.Buffer
	img := image.NewRGBA(image.Rect(0, 0, 2, 2))
	img.Set(0, 0, color.RGBA{20, 100, 255, 255})
	png.Encode(&data, img)
	return data.Bytes()
}
func request(t *testing.T, r report, files []attachment) *http.Request {
	t.Helper()
	var b bytes.Buffer
	writer := multipart.NewWriter(&b)
	field, _ := writer.CreateFormField("report")
	json.NewEncoder(field).Encode(r)
	for _, file := range files {
		field, _ := writer.CreateFormFile("file", file.Name)
		field.Write(file.Data)
	}
	writer.Close()
	req := httptest.NewRequest(http.MethodPost, "/v1/reports", &b)
	req.RemoteAddr = "192.0.2.12:1234"
	req.Header.Set("Content-Type", writer.FormDataContentType())
	return req
}
func TestDeliveredReportIncludesAllEvidence(t *testing.T) {
	files := []attachment{{"screen.png", pngFile()}, {"clip.mp4", append([]byte{0, 0, 0, 24}, []byte("ftypisom000000000000")...)}}
	var called bool
	discord := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		called = true
		if r.URL.Query().Get("wait") != "true" {
			t.Error("delivery confirmation not requested")
		}
		reader, err := r.MultipartReader()
		if err != nil {
			t.Error(err)
			w.WriteHeader(400)
			return
		}
		n := 0
		for {
			p, err := reader.NextPart()
			if err == io.EOF {
				break
			}
			if err != nil {
				t.Error(err)
				return
			}
			data, _ := io.ReadAll(p)
			if p.FormName() == "payload_json" {
				if !bytes.Contains(data, []byte("allowed_mentions")) {
					t.Error("mentions not restricted")
				}
			} else {
				if n == 0 && !bytes.Contains(data, []byte("Steps to reproduce")) {
					t.Error("full report missing")
				}
				if n > 0 && !bytes.Equal(data, files[n-1].Data) {
					t.Error("evidence changed")
				}
				n++
			}
		}
		if n != 3 {
			t.Errorf("files=%d", n)
		}
		w.Header().Set("Content-Type", "application/json")
		io.WriteString(w, `{"id":"123456","attachments":[{},{},{}]}`)
	}))
	defer discord.Close()
	s := &service{webhook: discord.URL, client: discord.Client(), clients: make(map[string]bucket), busy: make(chan struct{}, 4)}
	w := httptest.NewRecorder()
	s.ServeHTTP(w, request(t, fixture(), files))
	if !called || w.Code != 201 || !strings.Contains(w.Body.String(), `"delivered":true`) {
		t.Fatalf("%d %s", w.Code, w.Body.String())
	}
}
func TestInvalidReportsNeverReachDiscord(t *testing.T) {
	discord := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { t.Error("invalid report delivered") }))
	defer discord.Close()
	cases := []struct {
		name  string
		r     report
		files []attachment
	}{
		{"missing fields", report{Version: "1.1.1"}, nil},
		{"fake image", fixture(), []attachment{{"fake.png", []byte("not png")}}},
		{"unsupported file", fixture(), []attachment{{"sample.exe", pngFile()}}},
		{"too large", fixture(), []attachment{{"large.mp4", make([]byte, maxUpload+1)}}},
	}
	for _, item := range cases {
		t.Run(item.name, func(t *testing.T) {
			s := &service{webhook: discord.URL, client: discord.Client(), clients: make(map[string]bucket), busy: make(chan struct{}, 4)}
			w := httptest.NewRecorder()
			s.ServeHTTP(w, request(t, item.r, item.files))
			if w.Code != 400 {
				t.Errorf("code %d %s", w.Code, w.Body.String())
			}
		})
	}
}
func TestDiscordFailureCannotReportSuccess(t *testing.T) {
	for _, status := range []int{400, 429, 500} {
		t.Run(http.StatusText(status), func(t *testing.T) {
			discord := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(status) }))
			defer discord.Close()
			s := &service{webhook: discord.URL, client: discord.Client(), clients: make(map[string]bucket), busy: make(chan struct{}, 4)}
			w := httptest.NewRecorder()
			s.ServeHTTP(w, request(t, fixture(), nil))
			if w.Code != 502 || strings.Contains(w.Body.String(), `"delivered":true`) {
				t.Fatalf("%d %s", w.Code, w.Body.String())
			}
		})
	}
}
func TestAttachmentConfirmationRequired(t *testing.T) {
	discord := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, `{"id":"1234","attachments":[]}`)
	}))
	defer discord.Close()
	s := &service{webhook: discord.URL, client: discord.Client(), clients: make(map[string]bucket), busy: make(chan struct{}, 4)}
	w := httptest.NewRecorder()
	s.ServeHTTP(w, request(t, fixture(), nil))
	if w.Code != 502 {
		t.Fatalf("%d %s", w.Code, w.Body.String())
	}
}
func TestPerClientAndGlobalLimits(t *testing.T) {
	s := &service{clients: make(map[string]bucket)}
	for i := 0; i < 5; i++ {
		if !s.allow("192.0.2.10") {
			t.Fatal("early limit")
		}
	}
	if s.allow("192.0.2.10") {
		t.Fatal("limit bypassed")
	}
	if !s.allow("192.0.2.11") {
		t.Fatal("unrelated client blocked")
	}
	for i := 6; i < 100; i++ {
		if !s.allow(fmt.Sprintf("198.51.100.%d", i)) {
			t.Fatal("early global limit")
		}
	}
	if s.allow("203.0.113.1") {
		t.Fatal("global limit bypassed")
	}
}
func TestForwardedAddressRequiresTrustedProxy(t *testing.T) {
	_, proxy, _ := net.ParseCIDR("172.29.17.2/32")
	s := &service{proxy: proxy, clients: make(map[string]bucket), busy: make(chan struct{}, 4)}
	for _, remote := range []string{"192.0.2.4:1234", "172.29.17.2:1234"} {
		r := request(t, report{}, nil)
		r.RemoteAddr = remote
		r.Header.Set("X-Forwarded-For", "203.0.113.7")
		w := httptest.NewRecorder()
		s.ServeHTTP(w, r)
		if w.Code != 400 {
			t.Fatal(w.Code)
		}
	}
	if s.clients["192.0.2.4"].Count != 1 || s.clients["203.0.113.7"].Count != 1 {
		t.Fatal("proxy address policy failed")
	}
	r := request(t, report{}, nil)
	r.RemoteAddr = "172.29.17.2:1234"
	r.Header.Set("X-Forwarded-For", "203.0.113.7, 198.51.100.1")
	w := httptest.NewRecorder()
	s.ServeHTTP(w, r)
	if w.Code != 400 || s.clients["203.0.113.7"].Count != 1 {
		t.Fatal("ambiguous forwarding chain accepted")
	}
}
