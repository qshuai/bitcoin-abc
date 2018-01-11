### TOOL:verify which pubkey keys contribute to multisig

#### Notice:

- just support BCH multisig verify
- you shoud input current rawtx string, reference rawtx string, vin index and the current vin ammount
- you must ensure your input correctly

#### Usages:

- install all bitcoin-abc dependencies [official link](https://github.com/Bitcoin-ABC/bitcoin-abc/blob/master/doc/) 
- cd this repository path
- ./autogen.sh
- ./configyre
- make
- cd src/test
- ./test_bitcoin --run_test=transaction_tests/parse_tx

#### Bugs:

- There is a bytes length limit(up to 1024 bytes) on macOS platform in command line
