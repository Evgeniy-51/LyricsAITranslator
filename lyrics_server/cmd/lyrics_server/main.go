package main

import (
	"flag"
	"log"

	"lyrics-plugin/lyrics_server/internal/server"
)

func main() {
	host := flag.String("host", "0.0.0.0", "listen host (0.0.0.0 for LAN)")
	port := flag.Int("port", 8765, "listen port")
	token := flag.String("token", "", "optional browser auth token")
	flag.Parse()

	srv := server.New(server.Config{
		Host:      *host,
		Port:      *port,
		AuthToken: *token,
	})
	log.Fatal(srv.ListenAndServe())
}
