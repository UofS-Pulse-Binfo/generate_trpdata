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
      'marker-type' => 'A free-text string to use as the marker type (Default=Cool Technology).',
      'load-mviews' => 'If present the ND Genotypes mview_ndg_variants and mviews_ndg_calls will be loaded as the data is generated (Default=FALSE).',
      'truncate-existing' => 'Provide this option to first remove all existing genotype, stock, feature data.',
    ),
  );

  $items['generate-phenotypes'] = array(
    'description' => 'Generate Phenotypic Data.',
    'aliases' => array('gen-pheno'),
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

  // For use in filenames.
  $site = variable_get('site_name', 'Drupal');
  $site = preg_replace("/([^\w\d])/", '', $site);

  // Retrieve Options:
  //-------------------------
  $out_stub = drush_get_option('out-stub', '/tmp/massive_genotype_dataset.' . $site);
  $project_name = drush_get_option('project', NULL);

  $country = drush_get_option('country', NULL);
  $num_samples = drush_get_option('num-samples', 100);

  $num_chr = drush_get_option('num-chr', 10);
  $num_per_chr = drush_get_option('num-per-chr', 500000);
  $start_chr_num = drush_get_option('start-chr', 1);
  $max_chr_num = $start_chr_num + $num_chr -1;

  $copy_chunk_size = drush_get_option('chunk-size', 50000);
  $marker_type = drush_get_option('marker-type','Cool Technology');
  $load_mviews = drush_get_option('load-mviews', FALSE);
  $truncate = drush_get_option('truncate-existing', FALSE);


  $mview_calls_table = 'mview_ndg_tripalus_calls';
  $mview_variants_table = 'mview_ndg_tripalus_variants';

  // Ensure the genotype_call table is present.
  print "Creating genotype_call if it doesn't already exist...\n";
  chado_create_custom_table('genotype_call', nd_genotypes_genotype_call_schema_template());

  // Truncate data is asked to.
  if ($truncate) {
    print "Truncating existing data...\n";
    chado_query('TRUNCATE {feature} CASCADE');
    chado_query('TRUNCATE {stock} CASCADE');
    chado_query('TRUNCATE {project} CASCADE');
    chado_query('TRUNCATE {'.$mview_variants_table.'}');
    chado_query('TRUNCATE {'.$mview_calls_table.'}');
  }

  drush_log('+ Current Database Size: ' . trpdata_get_database_size(), 'status');

  //--------------------
  // Materialized views setup.
  if ($load_mviews) {

    print "Preparing materialized views...\n";

    // Ensure that the mviews exist...
    print "  - Creating $mview_calls_table if it doesn't already exist...\n";
    nd_genotypes_create_mview_ndg_calls($mview_calls_table);
    print "  - Creating $mview_variants_table if it doesn't already exist...\n";
    nd_genotypes_create_mview_ndg_variants($mview_variants_table);

    // Drop any indexes.
    print "  - Dropping indexes on materialized views...\n";
    nd_genotypes_drop_indexes('mview_ndg_tripalus_calls');
    nd_genotypes_drop_indexes('mview_ndg_tripalus_variants');
  }

  //--------------------
  // CV Terms
  // Check all the cvterms and warn if they are missing!
  $missing_terms = FALSE; $missing_seq_terms = FALSE;
  $types = array(
    array('name' => 'marker_type', 'cv_name' => 'feature_property'),
    array('name' => 'SNP', 'cv_name' => 'sequence'),
    array('name' => 'genetic_marker', 'cv_name' => 'sequence'),
    array('name' => 'is_marker_of', 'cv_name' => 'feature_relationship'),
    array('name' => 'DNA', 'cv_name' => 'stock_type'),
    array('name' => 'Accession', 'cv_name' => 'stock_type'),
    array('name' => 'is_extracted_from', 'cv_name' => 'stock_relationship'),
  );
  foreach ($types as $type) {
    // Check if it exists.
    $exists = chado_query('SELECT cvterm_id FROM {cvterm} WHERE name=:name and cv_id IN (select cv_id FROM {cv} WHERE name=:cv)',
      array(':name' => $type['name'], ':cv' => $type['cv_name']))->fetchField();

    // If it exists then save the cvterm for later.
    if ($exists) {
      $options['cvterm'][ $type['name'] ] = $exists;
    }
    // Otherwise, return an error asking them to add it.
    else {
      $missing_terms[ $type['name'] ] = $type;
      drush_log('Missing cvterm "' . $type['name'] . '" (cv: "' . $type['cv_name'] . '")', 'error');
    }
  }

  // If we were missing any cvterm then we should quit now to let them add them.
  if ($missing_terms) {
    if (drush_confirm('Would you like me to add the terms for you? (Will not add sequence ontology terms)')) {

      foreach ($missing_terms as $term) {

        // If not from the sequence ontology then create the term.
        if ($term['cv_name'] != 'sequence') {
          $term['id'] = 'tripal:' . $term['name'];
          $obj = tripal_insert_cvterm($term);

          // If we were successful then remove the term.
          if ($obj) {
            unset($missing_terms[ $term['name'] ]);
          }
        }
      }

      // If we still have missing terms then abort.
      if ($missing_terms) {
        return drush_set_error('Missing cvterms. For the sequence ontology terms we suggest you load the entire sequence ontology via the OBO loader. For all other terms, add them through the UI at Tripal > Chado Modules > Controlled Vocabularies > Terms.');
      }
    }
    else {
      return drush_set_error('Missing cvterms. For the sequence ontology terms we suggest you load the entire sequence ontology via the OBO loader. For all other terms, add them through the UI at Tripal > Chado Modules > Controlled Vocabularies > Terms.');
    }
  }

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

  //--------------------
  // Germplasm and Samples.
  print "Generating Samples and Germplasm...\n";
  $options = array('num' => $num_samples, 'insert' => TRUE, 'generate germplasm' => TRUE);
  $samples = generate_nd_samples($options);
  if (sizeof($samples)) { drush_log(" - Created ".sizeof($samples).' samples to attach genotypes to.', 'success'); }
  else { return drush_set_error("\tUnable to insert samples."); }

  drush_log('+ Current Database Size: ' . trpdata_get_database_size(), 'status');

  //=======================
  // Markers and Variants
  print "Generating Markers and Variants...\n";

  $organism_id = trpdata_get_default_organism(array('id only' => TRUE));

  for($chr_num=$start_chr_num; $chr_num <= $max_chr_num; $chr_num++) {

    // Determine the name of the chromosome and pick a random start position.
    $chr = 'Chr' . $chr_num;
    $pos = rand(1,300);
    print " - Generating markers & variants for $chr...\n";

    //--------------------
    // Chromosome.
    print "   - Creating the chromosome.\n";
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
      'chr_id' => $chr_id,
      'out_stub' => $out_stub,
      'organism_id' => $organism_id,
      'project_id' => $project_id,
      'marker_type' => $marker_type,
      'load mview' => $load_mviews,
    );
    generate_nd_markers($options);

  }

  drush_log('+ Current Database Size: ' . trpdata_get_database_size(), 'status');

  //---------------------
  // Genotype Calls.
  print "Generating Genotype Calls...\n";
  $options = array(
    'alleles' => $alleles,
    'project_id' => $project_id,
    'samples' => $samples,
    'genotype_ids' => $genotypes,
    'load mview' => $load_mviews,
    'out_stub' => $out_stub,
  );
  generate_nd_genotype_calls($options);

  if ($load_mviews) {
    $partition = 'tripalus';

    print "Finishing the materialized views...\n";

    // Create the indexes you dropped earlier.
    print " - Creating Indexes\n";
    nd_genotypes_create_mview_ndg_calls_indexes('mview_ndg_'.$partition.'_calls');

    // Create the indexes you dropped earlier.
    print " - Creating Indexes\n";
    nd_genotypes_create_mview_ndg_variants_indexes('mview_ndg_'.$partition.'_variants');

    // Create the indexes you dropped earlier.
    print " - Sync'ing genotypes to the mview_ndg_germplasm_genotyped mview...";
    nd_genotypes_update_mview_ndg_germplasm_genotyped($partition, 5);

    // Update the cached lists.
    print " - Updating cached lists used for select boxes and what-not\n";
    nd_genotypes_update_mview_cache($partition);
  }

  drush_log('+ Final Database Size: ' . trpdata_get_database_size(), 'status');
}

/**
 * Drush Command: Generate Phenotypes
 */
function drush_generate_trpdata_generate_phenotypes() {

  // Create phenotypic data for all stocks associated with a given project
  // for the locations and years specified in the following array.
  // HARD-CODING FOR NOW TO MAKE QUICKER.
  $trait_id = 2787;
  $project_id = 4;
  $locations = array('Sutherland', 'Kernen Farm');
  $years = array(2013, 2014);

  $type_ids = array(
    'location' => 2500,
    'year' => 2790,
    'min' => 2791,
    'max' => 2792,
    'standard deviation' => 2793,
  );

  // Header
  print implode("\t", array('Location', 'Year', 'Line', 'Mean', 'Min', 'Max', 'Std. Dev.')) . "\n";

  // Get the list of stocks.
  $stocks = chado_query('SELECT s.stock_id, s.name FROM {project_stock} p LEFT JOIN {stock} s USING(stock_id) WHERE project_id=:id LIMIT 5',
    array(':id' => $project_id))->fetchAllKeyed();

  // Generate data for each location, year, stock combination.
  foreach ($locations as $location) {
    foreach ($years as $year) {
      foreach ($stocks as $stock_id => $stock_name) {

        // Generate quantitate data.
        $mean = rand(100,300) / 10;
        $dev = rand(20,50) / 10;
        $min = $mean - $dev - (rand(10,30)/10);
        $max = $mean + $dev + (rand(10,30)/10);

        // Print to the dataset file.
        print implode("\t", array($location, $year, $stock_name, $mean, $min, $max, $dev)) . "\n";

        // Come up with a uniquename for this measurement.
        $uniquename = implode('_', array($project_id, $location, $year, $stock_id, date('Ymd')));

        // Now, insert him into the database :-).
        //Phenotype
        $values = array(
          ':uniquename' => $uniquename,
          ':attr_id' => $trait_id,
          ':value' => $mean,
          ':project_id' => $project_id,
          ':stock_id' => $stock_id,
        );
        chado_query('INSERT INTO {phenotype} (uniquename, attr_id, value, project_id, stock_id) VALUES (:uniquename, :attr_id, :value, :project_id, :stock_id)',
          $values);
        $phenotype_id = chado_query('SELECT phenotype_id FROM {phenotype} WHERE uniquename=:uniquename AND attr_id=:attr_id AND value=:value AND project_id=:project_id AND stock_id=:stock_id',
          $values)->fetchField();

        // Phenotype Properties
        $properties = array(
          $type_ids['location'] => $location,
          $type_ids['year'] => $year,
          $type_ids['min'] => $min,
          $type_ids['max'] => $max,
          $type_ids['standard deviation'] => $dev
        );
        foreach ($properties as $type_id => $value) {
          $values = array(
            'phenotype_id' => $phenotype_id,
            'type_id' => $type_id,
            'value' => $value
          );
          chado_insert_record('phenotypeprop', $values);
        }
      }
    }
  }
}
