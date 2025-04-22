srcf=$(mktemp)

# for env, alias, func, trap, clear then apply each entry separately

# env
if [ -f '/tmp/__laplace_env' ]; then
  # cat '/tmp/__laplace_env'
  while IFS="" read -r k; do
    printf "unset %s\n" "$k" >> $srcf
  done < <(compgen -e)
  while IFS="" read -r -d '' kv; do
    printf "export %s\n" "$kv" >>  $srcf
  done < '/tmp/__laplace_env'
  rm '/tmp/__laplace_env'
fi

if [ -f '/tmp/__laplace_alias' ]; then
  # cat '/tmp/__laplace_alias'
  # alias
  while IFS="" read -r a; do
    printf "unalias %s\n" "$a" >>  $srcf
  done < <(compgen -a)
  while IFS="" read -r -d '' a; do
    printf "%s\n" "$a" >>  $srcf
  done < '/tmp/__laplace_alias'
  rm '/tmp/__laplace_alias'
fi

if [ -f '/tmp/__laplace_func' ]; then
  # cat '/tmp/__laplace_func'
  # func
  while IFS="" read -r f; do
    printf "unset %s\n" "$f" >>  $srcf
  done < <(compgen -A function)
  while IFS="" read -r -d '' f; do
    printf "%s\n" "$f" >> $srcf
  done < '/tmp/__laplace_func'
  rm '/tmp/__laplace_func'
fi

if [ -f '/tmp/__laplace_trap' ]; then
  # cat '/tmp/__laplace_trap'
  # trap
  for SIG in $(seq 0 64) ERR RETURN; do
    printf "trap - %s\n" "$SIG" >> $srcf # this also unregisters this current flush trap
  done
  while IFS="" read -r -d '' t; do
    printf "%s\n" "$t" >> $srcf
  done < '/tmp/__laplace_trap'
  rm '/tmp/__laplace_trap'
fi

if [ -f '/tmp/__laplace_shopt' ]; then
  # cat '/tmp/__laplace_shopt'
  # shopt automatically sets everything, no need to clear
  while IFS="" read -r s; do
    printf "%s\n" "$s" >> $srcf
  done < '/tmp/__laplace_shopt'
  rm "/tmp/__laplace_shopt"
fi

# cat $srcf > /root/modulefiles/debug
source $srcf
rm $srcf

# __laplace_postcommand
# Shell --> monitor communication
function __laplace_push() {
    exec 2220< <(while IFS="" read -r E; do
        printf "%s=" "$E"
        printf "$E=$(printenv $E)"
        printf "\0"
      done < <(compgen -e))
    exec 2221< <(while IFS="" read -r A; do
        printf "%s=" "$A"
        alias $A
        printf "\0"
      done < <(compgen -a)
    )
    exec 2222< <(
      printf "_="
      shopt -p
    ) # don't break shopt output, there's no point
    exec 2223< <(while IFS="" read -r F; do
        fname="$(printf "%s" "$F" | cut -d" " -f3-)"
        printf "%s=" "$fname"
        declare -f "$fname" # print function definition, note the exported functions will need this exact command, rather than the listed command "declare -fx <name>"

        # exported function names need extra attention
        if [[ "${F:0:11}" = "declare -fx" ]]; then
          printf "%s" "$F"
        fi
        printf "\0"
      done < <(declare -F)
    )

    # there is also a DEBUG trap but we are taking up that
    # the section here is to prevent using a subshell, we first generate the for loop
    # code to a temp file, then source that code in the current shell
    srcf=$(mktemp)

    tmpf=$(mktemp -u)
    exec 2224<>$tmpf
    for SIG in $(seq 0 64) ERR RETURN; do
      echo "trap -p ${SIG} >>${tmpf}">>$srcf
      echo "printf \"\\0\" >>${tmpf}">>$srcf
    done

    source $srcf
    rm $srcf

    echo "__laplace_push" | /bin/laplace \
    -i PROMPT_COMMAND -i __laplace -i PWD -i SHLVL \
    2220 env 2221 alias 2222 shopt 2223 func 2224 trap

    exec 2220<&-
    exec 2221<&-
    exec 2222<&-
    exec 2223<&-
    exec 2224<&-
}

function __laplace_precommand() {
  if [[ $BASH_COMMAND = "__laplace_postcommand" ]]; then
    return
  fi
  echo "$BASH_COMMAND" | /bin/laplace
}

__laplace_lineno=0
function __laplace_postcommand() {
  trap - DEBUG

  # for checkpoints, actually submit the entire state for diffing purposes

  if [[ $__laplace_lineno = '0' ]]; then
    echo "Initial workspace entry" | /bin/laplace
    __laplace_lineno=1
  fi

  __laplace_push

  trap '__laplace_precommand' DEBUG
}

PROMPT_COMMAND="__laplace_postcommand"

echo "Welcome to Laplace!"
