package main

import (
	"fmt"
	"sync"
	"time"

	"github.com/openrung/openrung/connectcore"
)

const (
	// logRingCapacity bounds the replayable history; the SSE connect replay
	// and /api/logs read the last 200 of it.
	logRingCapacity  = 500
	logReplayOnJoin  = 200
	subscriberBufLen = 64
)

type logEntry struct {
	Time time.Time `json:"time"`
	Line string    `json:"line"`
}

// eventHub fans engine events out to SSE subscribers and keeps the replayable
// log ring. It is written by the engine sink callbacks, which run
// synchronously under engine locks, so every mutation here completes before
// the sink returns and nothing on this path calls back into the engine.
type eventHub struct {
	mu      sync.Mutex
	logs    []logEntry // ring slice, trimmed to logRingCapacity
	subs    map[chan []byte]struct{}
	stateCh chan struct{}  // cap 1: a pending state broadcast, coalesced
	logCh   chan logEntry  // live-log stream for the broadcaster
}

func newEventHub() *eventHub {
	return &eventHub{
		subs:    make(map[chan []byte]struct{}),
		stateCh: make(chan struct{}, 1),
		logCh:   make(chan logEntry, 256),
	}
}

// signalState queues a state broadcast. Consecutive changes without a drain
// collapse into one broadcast — the snapshot is rebuilt from the engine at
// broadcast time, so the newest state always wins.
func (h *eventHub) signalState() {
	select {
	case h.stateCh <- struct{}{}:
	default:
	}
}

// appendLog records the entry and hands it to the broadcaster. The ring write
// is unconditional; the live send drops under backpressure (the engine must
// never block on streaming) with the ring keeping the line for /api/logs.
func (h *eventHub) appendLog(e logEntry) {
	h.mu.Lock()
	h.logs = append(h.logs, e)
	if excess := len(h.logs) - logRingCapacity; excess > 0 {
		copy(h.logs, h.logs[excess:])
		h.logs = h.logs[:len(h.logs)-excess]
	}
	h.mu.Unlock()
	select {
	case h.logCh <- e:
	default: // subscriber stream saturated; the ring still has it
	}
}

// tailLogs copies the newest n entries.
func (h *eventHub) tailLogs(n int) []logEntry {
	h.mu.Lock()
	defer h.mu.Unlock()
	if n > len(h.logs) {
		n = len(h.logs)
	}
	out := make([]logEntry, n)
	copy(out, h.logs[len(h.logs)-n:])
	return out
}

// subscribe registers a live frame channel and returns the log replay taken
// under the same lock, so the stream continues exactly where the replay ends:
// lines logged before the snapshot are in the replay, lines after land in ch
// in order.
func (h *eventHub) subscribe() (ch chan []byte, replay []logEntry) {
	ch = make(chan []byte, subscriberBufLen)
	h.mu.Lock()
	n := logReplayOnJoin
	if n > len(h.logs) {
		n = len(h.logs)
	}
	replay = append([]logEntry(nil), h.logs[len(h.logs)-n:]...)
	h.subs[ch] = struct{}{}
	h.mu.Unlock()
	return ch, replay
}

func (h *eventHub) unsubscribe(ch chan []byte) {
	h.mu.Lock()
	delete(h.subs, ch)
	h.mu.Unlock()
}

// publish delivers one pre-rendered SSE frame to every subscriber. A full
// subscriber channel means the client stopped reading, so the frame is
// dropped for that subscriber rather than stalling the engine path.
func (h *eventHub) publish(frame []byte) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for ch := range h.subs {
		select {
		case ch <- frame:
		default:
		}
	}
}

const (
	sseStateEvent = "state"
	sseLogEvent   = "log"
	ssePing       = ":ping"
)

func sseFrame(event string, data []byte) []byte {
	return []byte(fmt.Sprintf("event: %s\ndata: %s\n\n", event, data))
}

// coreSink is the connectcore.EventSink wiring the engine into the hub. Both
// methods only touch the hub — the engine forbids callbacks from the sink.
type coreSink struct {
	core *core
}

var _ connectcore.EventSink = coreSink{}

func (s coreSink) StateChanged(connectcore.State) { s.core.hub.signalState() }

func (s coreSink) Log(entry connectcore.LogEntry) {
	s.core.hub.appendLog(logEntry{Time: entry.Time, Line: entry.Line})
}
