package lyrics

import "testing"

func TestParseSynced(t *testing.T) {
	input := "[00:14.11] Is it getting better?\n[00:19.34] Or do you feel the same?"
	lines := ParseSynced(input)
	if len(lines) != 2 {
		t.Fatalf("got %d lines", len(lines))
	}
	if lines[0].TimeMs != 14110 {
		t.Fatalf("timeMs=%d", lines[0].TimeMs)
	}
	if lines[0].Text != "Is it getting better?" {
		t.Fatalf("text=%q", lines[0].Text)
	}
}
