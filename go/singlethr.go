package main

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"log"
	"os"
	"path"
	"runtime/pprof"
	"sort"
	"strconv"
)

func main() {
	input := os.Stdin
	if len(os.Args) > 2 {
		fmt.Fprintf(os.Stderr, "Usage: avg.go [INFILE]\n")
		os.Exit(1)
	} else if len(os.Args) == 2 {
		var err error
		input, err = os.Open(os.Args[1])
		if err != nil {
			log.Fatal(err)
		}
	}
	f, err := os.Create(path.Base(os.Args[0]) + ".pprof")
	if err != nil {
		log.Fatal(err)
	}
	defer f.Close()
	if err := pprof.StartCPUProfile(f); err != nil {
		log.Fatal(err)
	}
	defer pprof.StopCPUProfile()

	if err := avg(input); err != nil {
		log.Fatal(err)
	}
}

type record struct {
	total, min, max float64
	num             int64
}

func avg(in_ io.Reader) error {
	stats := make(map[string]record, 10000)
	in := bufio.NewReaderSize(in_, 64*1024)
	out := bufio.NewWriter(os.Stdout)
	for {
		line, err := in.ReadSlice('\n')
		if err == io.EOF {
			break
		} else if err != nil {
			return err
		}
		nameBytes, dataBytes, ok := bytes.Cut(line, []byte(";"))
		if !ok {
			return fmt.Errorf("invalid record: %q", line)
		}
		data, err := parsenum(dataBytes)
		if err != nil {
			return err
		}
		name := string(nameBytes)
		rec, found := stats[name]
		rec.total += data
		rec.num++
		if !found || data < rec.min {
			rec.min = data
		}
		if !found || data > rec.max {
			rec.max = data
		}
		stats[name] = rec
	}
	keys := make([]string, 0, len(stats))
	for k := range stats {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for i, k := range keys {
		if i == 0 {
			fmt.Fprintf(out, "{%s", k)
		} else {
			fmt.Fprintf(out, ", %s", k)
		}
		fmt.Fprintf(out, "=%.1f/%.1f/%.1f", stats[k].min, stats[k].total/float64(stats[k].num), stats[k].max)
	}
	fmt.Fprintln(out, "}")
	return out.Flush()
}

func parsenum(buf []byte) (float64, error) {
	return strconv.ParseFloat(string(buf[:len(buf)-1]), 64)
}
