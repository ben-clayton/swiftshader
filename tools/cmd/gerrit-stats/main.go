// Copyright 2020 The SwiftShader Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// gerrit-stats gathers statistics about changes made to SwiftShader.
package main

import (
	"flag"
	"fmt"
	"net/url"
	"os"
	"regexp"
	"strings"
	"time"

	"github.com/andygrunwald/go-gerrit"
)

const (
	yyyymmdd  = "2006-01-02"
	gerritURL = "https://swiftshader-review.googlesource.com/"
)

var (
	userFlag    = flag.String("user", "", "user name / email")
	afterFlag   = flag.String("after", "", "start date")
	beforeFlag  = flag.String("before", "", "end date")
	daysFlag    = flag.Int("days", 182, "interval in days (used if --after is not specified)")
	verboseFlag = flag.Bool("v", false, "verbose mode - lists all the changes")
)

func main() {
	flag.Parse()
	if err := run(); err != nil {
		fmt.Fprint(os.Stderr, err)
		os.Exit(1)
	}
}

func run() error {
	var after, before time.Time
	var err error
	user := *userFlag
	if user == "" {
		return fmt.Errorf("Missing required 'user' flag")
	}
	if *beforeFlag != "" {
		before, err = time.Parse(yyyymmdd, *beforeFlag)
		if err != nil {
			return fmt.Errorf("Couldn't parse before date: %w", err)
		}
	} else {
		before = time.Now()
	}
	if *afterFlag != "" {
		after, err = time.Parse(yyyymmdd, *afterFlag)
		if err != nil {
			return fmt.Errorf("Couldn't parse after date: %w", err)
		}
	} else {
		after = before.Add(-time.Hour * time.Duration(24**daysFlag))
	}

	client, err := gerrit.NewClient(gerritURL, nil)
	if err != nil {
		return fmt.Errorf("Couldn't create gerrit client: %w", err)
	}

	submittedQuery := []string{
		"status:merged",
		"owner:" + user,
		"after:" + date(after),
		"before:" + date(before),
	}

	submitted, _, err := client.Changes.QueryChanges(&gerrit.QueryChangeOptions{
		QueryOptions: gerrit.QueryOptions{
			Query: []string{strings.Join(submittedQuery, "+")},
		},
	})
	if err != nil {
		return fmt.Errorf("Query failed: %w", err)
	}

	reviewQuery := []string{
		"status:merged",
		"commentby:" + user,
		"-owner:" + user,
		"after:" + date(after),
		"before:" + date(before),
	}

	reviewed, _, err := client.Changes.QueryChanges(&gerrit.QueryChangeOptions{
		QueryOptions: gerrit.QueryOptions{
			Query: []string{strings.Join(reviewQuery, "+")},
		},
	})
	if err != nil {
		return fmt.Errorf("Query failed: %w", err)
	}

	blacklist := []*regexp.Regexp{
		regexp.MustCompile("clang-format"),
		regexp.MustCompile("Update .* to [0-9a-f]*"),
		regexp.MustCompile("Squashed '.*' [changes|content]"),
		regexp.MustCompile("Revert .*"),
		regexp.MustCompile("Initial drop of LLVM 10"),
	}
	ignore := func(s string) bool {
		for _, re := range blacklist {
			if re.MatchString(s) {
				return true
			}
		}
		return false
	}

	insertions, deletions := 0, 0
	warnings := []string{}
	for _, change := range *submitted {
		if ignore(change.Subject) {
			continue
		}
		if change.Insertions > 10000 {
			warnings = append(warnings, fmt.Sprintf("Change: %6.v %v has an excessive number of insertions (%d)", change.Number, change.Subject, change.Insertions))
		}
		if change.Deletions > 10000 {
			warnings = append(warnings, fmt.Sprintf("Change: %6.v %v has an excessive number of deletions (%d)", change.Number, change.Subject, change.Deletions))
		}
		insertions += change.Insertions
		deletions += change.Deletions
	}

	fmt.Printf("Between %v and %v, %v:\n", date(after), date(before), user)
	fmt.Printf("  Submitted %v changes (LOC: %v+, %v-) \n", len(*submitted), insertions, deletions)
	fmt.Printf("  Reviewed %v changes\n", len(*reviewed))
	fmt.Printf("\n")

	if *verboseFlag {
		fmt.Printf("Submitted changes:\n")
		for i, change := range *submitted {
			fmt.Printf("%3.1v: %6.v %v (LOC: %v+, %v-)\n", i, change.Number, change.Subject, change.Insertions, change.Deletions)
		}
		fmt.Printf("\n")
		fmt.Printf("Reviewed changes:\n")
		for i, change := range *reviewed {
			fmt.Printf("%3.1v: %6.v %v (LOC: %v+, %v-)\n", i, change.Number, change.Subject, change.Insertions, change.Deletions)
		}
	}

	fmt.Printf("\n")
	fmt.Printf("Submitted query: %vq/%v\n",
		gerritURL, url.QueryEscape(strings.Join(submittedQuery, "+")))
	fmt.Printf("Review query: %vq/%v\n",
		gerritURL, url.QueryEscape(strings.Join(reviewQuery, "+")))

	if len(warnings) > 0 {
		fmt.Printf("\nWarning:\n")
		for _, warning := range warnings {
			fmt.Println(warning)
		}
	}
	return nil
}

func today() time.Time {
	return time.Now()
}

func date(t time.Time) string {
	return t.Format(yyyymmdd)
}
