# data2ddl
command line tool guessing a table's matching CREATE TABLE statement with optimal data types from a CSV file. <p>
Explain:<p>
Usage is data2ddl [-coldel=<sep>] [-chardel=<chardel>] [-recdel=<recdel>] [-notitle][-debug[:<n>]][-tbname:<tbname>][-verbose] <file name> [> outfile]<p>
e. g.    data2ddl -coldel:(SEMI|COMMA|BAR|\x7c|','),' -chardel:(APO|QUOTE) myfile.txt   >myfile.ddl<p>
deducts the data type and the column name from first line and following literals in a file.<p>
   .
The extension of the infile is also used to set the default column delimiter. .csv leads to comma. .bsv leads to vertical bar. .tsv leads to tab. .ssv leads to semicolon. Character enclosing default is no character enclosing (empty string).<p>

