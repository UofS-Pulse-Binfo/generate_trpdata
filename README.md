# Generate Tripal Data
Generate various types of data for testing Tripal functionality.

Currently Supports:
- Stocks (`drush generate-stocks`): You can specify the type, organism and use tokens to generate the name and uniquename. Random names are choosen from http://uinames.com
- Genotypes (`drush generate-genotypes`): You can specify how many samples and how many variants to generate genotype calls for. Calls are stored in the custom genotype_call table as used by the [ND Genotypes Module](https://github.com/UofS-Pulse-Binfo/nd_genotypes). Record layout described here: https://github.com/UofS-Pulse-Binfo/nd_genotypes/wiki/How-to-Store-your-Data#method-2-custom-genotype-call-table
