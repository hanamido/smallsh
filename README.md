A program that simulates a mini-shell, with a CLI similar to other well-known shells such as bash. 

<b>Main Features:</b> 
<li>Most shell commands such as exit, cd, echo, etc.</li>
<li>& operator allows for commands to be ran in the background</li>
<li>Users will be notified of errors in their input</li>
<li>'~/' at the beginning of any word will be replaced with the value of the HOME environment.</li>
<li>'$$' anywhere in a word will be replaced with the process ID of the smallsh process.</li>
<li>'$?' anywhere in a word will be replaced with the exit status of the last foreground command.</li>
<li>'$!' anywhere in a word will be replaced with the process ID of the most recent background process.</li>
<li>Input and output redirection of files</li>
<li>Handling of SIGINT and SIGTSTP signals</li>
