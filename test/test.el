(defvar gb-test-dir default-directory)

(defun gb-download-test-suite (url dest-dir)
  (unless (file-exists-p dest-dir)
    (make-directory dest-dir)
    (let ((default-directory (expand-file-name dest-dir)))
      (shell-command (format "curl -L %s | tar xz --strip=1" url)))))

(defun gb-download-test-suites ()
  (interactive)
  (gb-download-test-suite
   "https://api.github.com/repos/retrio/gb-test-roms/tarball"
   "blargg")
  (gb-download-test-suite
   "https://gekkio.fi/files/mooneye-test-suite/mts-20240127-1204-74ae166/mts-20240127-1204-74ae166.tar.gz"
   "mooneye"))

(defun gb-get-mooneye-test-roms ()
  (let* ((mooneye/acceptance
	  (mapcar
	   (lambda (f) (file-relative-name f gb-test-dir))
	   (directory-files-recursively
	    (file-name-concat gb-test-dir "mooneye" "acceptance") ".gb$")))
	 (non-dmg-tests
	  '("mooneye/acceptance/boot_div2-S.gb"
	    "mooneye/acceptance/boot_div-dmg0.gb"
	    "mooneye/acceptance/boot_div-S.gb"
	    "mooneye/acceptance/boot_hwio-dmg0.gb"
	    "mooneye/acceptance/boot_hwio-S.gb"
	    "mooneye/acceptance/boot_regs-dmg0.gb"
	    "mooneye/acceptance/boot_regs-mgb.gb"
	    "mooneye/acceptance/boot_regs-sgb2.gb"
	    "mooneye/acceptance/boot_regs-sgb.gb")))
    (sort (seq-difference mooneye/acceptance non-dmg-tests) 'string<)))

(defun gb-run-cmd (cmd &rest args)
  (let ((default-directory gb-test-dir))
    (shell-command (apply 'format cmd args))))

(defun gb-return-code-to-string (return-code)
  (pcase return-code
    (11 "PASS")
    (13 "FAIL")
    (15 "TIMEOUT")
    (_ "CRASH")))

(defface gb-passed-test
  '((t (:background "green" :foreground "black")))
  "Face for passed test")

(defface gb-failed-test
  '((t (:background "red" :foreground "white")))
  "Face for failed test")

(defun gb-test-results-map ()
  (let ((m (make-sparse-keymap)))
    (suppress-keymap m)
    (keymap-set m "RET" 'gb-run-test-under-point)
    (keymap-set m "s" 'gb-open-test-source-code)
    (keymap-set m "n" 'next-line)
    (keymap-set m "p" 'previous-line)
    (keymap-set m "q" 'kill-this-buffer)
    m))

(defun gb-run-test-under-point ()
  (interactive)
  (move-beginning-of-line nil)
  (let ((test (thing-at-point 'filename)))
    (gb-run-cmd (format "../src/gb %s" test))))

(defun gb-open-test-source-code ()
  (interactive)
  (move-beginning-of-line nil)
  (let* ((test (thing-at-point 'filename))
	 (path (string-replace "mooneye/" "" (file-name-sans-extension test)))
	 (url (format "https://github.com/Gekkio/mooneye-test-suite/blob/main/%s.s" path)))
    (browse-url url)))

(defun gb-test-mooneye ()
  (interactive)
  (if (not (eq (gb-run-cmd "make") 0))
      (error "build failed"))
  (when (get-buffer "*test results*")
    (kill-buffer "*test results*"))
  (with-current-buffer (get-buffer-create "*test results*")
    (use-local-map (gb-test-results-map))
    (let* ((test-roms (gb-get-mooneye-test-roms))
	   (total-tests (length test-roms))
	   (tests-passed
	    (cl-loop for rom-path in test-roms
		     count
		     (let* ((return-code (gb-run-cmd "./gb-test %s --mooneye" rom-path))
			    (status (gb-return-code-to-string return-code)))
		       (insert (format "%-60s %s\n" rom-path status))
		       (string= status "PASS")))))
      (whitespace-cleanup)
      (highlight-phrase "FAIL\\|CRASH" 'gb-failed-test)
      (highlight-phrase "PASS" 'gb-passed-test)
      (beginning-of-buffer)
      (insert (format "%s / %s tests passed\n\n" tests-passed total-tests))
      (beginning-of-buffer)
      (next-line)
      (hl-line-mode))
    (switch-to-buffer (current-buffer))))
