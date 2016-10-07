<?php
/**
 *
 */

/**
 * Implements hook_drush_command().
 */
function generate_trpdata_drush_command() {
  $items = array();

  $items['generate-stocks'] = array(
    'description' => 'Generate chado stocks. Use --print-names if you would like ths names and stock_ids of the stocks created ;-).',
    'aliases' => array('gen-stock'),
    'arguments' => array(
      'type_id' => 'The type of stock',
    ),
    'options' => array(
      'num' => 'The number of stocks you would like to generate (Default=1).',
      'organism_id' => 'The organism the stock is from (Default=Tripalus databasica)',
      'country' => 'The country you want to restrict randomly generated names to. See http://uinames.com/ for options.',
      'name' => 'The name of the stock. Please include tokens like [name], [first], [last] to allow randomized names to be used.',
      'uniquename' => 'The unique name of the stock. Please include tokens like [name], [first], [last] to allow randomized names to be used.',
      'print-names' => 'Print the names and stock_ids for the stocks created (Default=FALSE).'
    ),
  );

  $items['generate-genotypes'] = array(
    'description' => 'Generate Genotypic Data.',
    'aliases' => array('gen-geno'),
    'options' => array(
      'out-stub' => "The full path stub for any files used with copy. Default is '/tmp/massive_genotype_dataset'.",
      'num-samples' => 'The number of samples/germplasm (Default=100).',
      'num-chr' => 'The number of chromosomes to generate (Default=10).',
      'num-per-chr' => 'The number of variants per chromosome (Default=500,000).',
      'chunk-size' => 'The number of records per COPY command (Default=50,000).',
      'country' => 'The name of the country to restrict stock names to.
        Possible options include Canada, France, Germany, India, Russia, United Stated.
        For a complete list see http://uinames.com/',
      'project' => 'The name of the project to use. The project will be created if it\'s
        not already and you will be prompted if you would like to use germplasm already
        associated with an existing project.',
    ),
  );

  return $items;
}

/**
 * Drush Command: Generate Stocks.
 */
function drush_generate_trpdata_generate_stocks($type_id) {

  require_once('api/generate.api.inc');

  // Get options from drush.
  $num = drush_get_option('num', 1);
  $organism_id = drush_get_option('organism_id', trpdata_get_default_organism(array('id only' => TRUE)));
  $country = drush_get_option('country', NULL);
  $name = drush_get_option('name', NULL);
  $uniquename = drush_get_option('uniquename', NULL);
  $print_names = drush_get_option('print-names', FALSE);
  $insert = drush_get_option('insert', TRUE);

  // Values for the api command.
  $values = array(
    'organism_id' => $organism_id,
    'type_id' => $type_id,
  );
  if ($name) $values['name'] = $name;
  if ($uniquename) $values['uniquename'] = $uniquename;

  // Options for the api call.
  $options = array('num' => $num, 'insert' => $insert);
  if ($country) $options['country'] = $country;

  $stocks = generate_chado_stock($values, $options);

  if ($print_names AND $insert) {
    foreach ($stocks as $s) {
      print "\t" . $s->name;
      if (isset($s->stock_id)) {
        print ' ('. $s->stock_id . ")\n";
      }
      else { print "\n"; }
    }
    print "\n";
  }

  // Always print if they didn't ask you to insert.
  if (!$insert) {
    foreach ($stocks as $s) {
      print '"' . implode('","',array($s->name, $s->uniquename, $s->organism_id, $s->type_id)) . '"' . "\n";
    }
    print "\n";
  }

  if ($insert) {
    drush_log('Created '.sizeof($stocks).' stocks.', 'success');
  }
}

/**
 * Drush Command: Generate Genotypes.
 */
function drush_generate_trpdata_generate_genotypes() {

  require_once('api/generate.api.inc');
  require_once('api/generate.genotypes.api.inc');

  // Retrieve Options:
  //-------------------------
  $out_stub = drush_get_option('out-stub', '/tmp/massive_genotype_dataset');
  $project_name = drush_get_option('project', NULL);

  $country = drush_get_option('country', NULL);
  $num_samples = drush_get_option('num-samples', 100);

  $num_chr = drush_get_option('num-chr', 10);
  $num_per_chr = drush_get_option('num-per-chr', 500000);
  $start_chr_num = drush_get_option('start-chr', 1);
  $max_chr_num = $start_chr_num + $num_chr -1;

  $copy_chunk_size = drush_get_option('chunk-size', 50000);

  $marker_type = 'Exome Capture';
  $marker_stub = 'exome_' . date('YMd');

  // Ensure cvterms are present
  // @todo implement this.

  $transaction = db_transaction();
  try{

    //--------------------
    // Project
    // Create a project to group the genotype calls.
    // Note: A project will be selected if it already exists.
    if ($project_name) {
      $values = array(
        'name' => $project_name,
      );
    }
    else {
      $values = array(
        'name' => 'Massive Genotype Set: '.date('YMd'),
        'description' => 'A project which groups together a massive genotype dataset.',
      );
    }
    $r = chado_select_record('project',array('*'), $values);
    if (!$r) {
      $values['description'] = 'A project which groups together a massive genotype dataset.';
      $r = chado_insert_record('project', $values);

      $project_id = $r['project_id'];
      drush_log('Created a project (' .$values['name'] . ': ' .$project_id.')', 'success');
    }
    else {
      $project_id = $r[0]->project_id;
      drush_log('Re-used existing project (' .$values['name'] . ': ' .$project_id.')', 'success');
    }

    // @todo Implement re-use of stocks attached to the current project.

    // Generate germplasm and samples.
    print "Generating Samples and Germplasm...\n";
    $options = array('num' => $num_samples, 'generate germplasm' => TRUE);
    $samples = generate_nd_samples($options);
    $sample_ids = array_keys($samples);
    if (sizeof($samples)) { drush_log(" - Created ".sizeof($samples).' samples to attach genotypes to.', 'success'); }
    else { return drush_set_error("\tUnable to insert samples."); }

    //--------------------
    // Genotypes
    // Save all the genotype combinations and keep the genotype_ids for later.
    // Note: Genotype_ids will be looked up if they already exist.
    print "Ensuring SNP genotypes are available...\n";
    $alleles = array('A' => 'A', 'T' => 'T', 'C' => 'C', 'G' => 'G');
    $genotypes = array();
    foreach ($alleles as $A) {
      foreach ($alleles as $B) {

        $call = $A . $B;
        $values = array(
          'name' => $call,
          'uniquename' => $call,
          'description' => $call,
        );
        $r = chado_select_record('genotype',array('*'), $values);
        if (!$r) {
          $values['type_id'] = $variant_type_id;
          $r = chado_insert_record('genotype', $values);
        }
        else {
          $r = (array) $r[0];
        }
        $genotypes[$call] = $r['genotype_id'];

      }
    }
    if (sizeof($genotypes)) { drush_log(" - " . sizeof($genotypes).' SNP genotypes available.', 'success'); }
    else { return drush_set_error("\tUnable to find or insert genotypes."); }

    //=======================
    // Now handle each chromosome separately
    print "\nNow starting with the remaining data one chromosome at a time ($start_chr_num to $max_chr_num).\n";

    $organism_id = trpdata_get_default_organism(array('id only' => TRUE));

    for($chr_num=$start_chr_num; $chr_num <= $max_chr_num; $chr_num++) {

      // Determine the name of the chromosome and pick a random start position.
      $chr = 'Chr' . $chr_num;
      $pos = rand(1,300);
      print "\nGenerating data for $chr...\n";

      //--------------------
      // Chromosome.
      print " - Creating the chromosome.\n";
      $values = array(
        'name' => $chr,
        'uniquename' => $chr,
        'organism_id' => $organism_id,
        'type_id' => array(
          'name' => 'chromosome',
          'cv_id' => array('name' => 'sequence'),
        ),
      );
      $r = chado_insert_record('feature', $values);
      $chr_id = $r['feature_id'];

      //--------------------
      // Variants/Markers.
      $options = array(
        'num_per_chr' => $num_per_chr,
        'chr' => $chr,
        'out_stub' => $out_stub,
        'organism_id' => $organism_id,
        'project_id' => $project_id,
      );
      generate_nd_markers($options);

      //---------------------
      // Genotype Calls.
      // Readt to start here!
    }
  }
  catch (Exception $e) {
    $transaction->rollback();
    watchdog_exception('my_type', $e);
  }

  $transaction->rollback();
}
