package main

import (
	"flag"
	"io"
	"log"
	"net"
	"os"
	"path/filepath"
	"time"
)

var (
	exeDir  = filepath.Dir(os.Args[0])
	addr    = flag.String("addr", ":19020", "Address of socket")
	logpath = flag.String("log", filepath.Join(exeDir, "dbg-log.txt"), "File path to log")
)

const (
	maxAttempts = 60
)

type spy struct {
	prefix string
	r      io.Reader
}

func (s spy) Read(p []byte) (n int, err error) {
	n, err = s.r.Read(p)
	log.Printf("%v %v", s.prefix, string(p[:n]))
	return n, err
}

func main() {
	var stdin io.Reader = os.Stdin
	var stdout io.Writer = os.Stdout
	os.Stdout, os.Stdin = nil, nil

	if *logpath != "" {
		file, err := os.Create(*logpath)
		if err == nil {
			log.SetOutput(file)
			os.Stdout = file
			os.Stderr = file
			log.Printf("Connecting to '%v'", *addr)
			defer file.Close()
		}
	}

	flag.Parse()

	var conn net.Conn
	for i := 0; i < maxAttempts; i++ {
		c, err := net.Dial("tcp", *addr)
		if err == nil {
			conn = c
			defer c.Close()
			break
		}
		log.Printf("Unable to connect to '%v': %v\n", *addr, err)
		time.Sleep(time.Second)
	}
	if conn == nil {
		log.Printf("Failed to connect to '%v' after %v attempts\n", *addr, maxAttempts)
		os.Exit(1)
	}

	log.Printf("Piping stdin and stdout to/from '%v'", *addr)

	var in io.Reader = conn
	var out io.Writer = conn

	in = spy{"DGB", in}
	stdin = spy{"IDE", stdin}

	go io.Copy(out, stdin)
	io.Copy(stdout, in)
}
