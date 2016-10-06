<?php
/**
 *
 */

/**
 * Implements hook_drush_command().
 */
function generate_trpdata_drush_command() {
  $items = array();

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

  // Ensure Tripal organism is present.
  // @todo implement this.

  // Retrieve IDs needed for foreign keys.
  $marker_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='genetic_marker'")->fetchField();
  $variant_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='SNP'")->fetchField();
  $stockrel_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='is_extracted_from'")->fetchField();
  $organism_id = chado_query("SELECT organism_id FROM {organism} WHERE genus='Lens' AND species='culinaris'")->fetchField();
  $feature_relationship_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='is_marker_of'")->fetchField();
  $featureprop_type_id = chado_query("SELECT cvterm_id FROM {cvterm} WHERE name='marker_type'")->fetchField();

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
      drush_log('Created a project (' .$values['name'] . ': ' .$project_id.')');
    }
    else {
      $project_id = $r[0]->project_id;
      drush_log('Re-used existing project (' .$values['name'] . ': ' .$project_id.')');
    }

    // @todo Implement re-use of stocks attached to the current project.

    // Generate germplasm and samples.
    print "Generating Samples and Germplasm...\n";
    $options = array('num' => $num_samples, 'generate germplasm' => TRUE);
    $samples = generate_nd_samples($options);
    $sample_ids = array_keys($samples);
    if (sizeof($samples)) { drush_log('Created '.sizeof($samples).' samples to attach genotypes to.', 'success'); }
    else { return drush_set_error('Unable to insert samples.'); }

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
    if (sizeof($genotypes)) { drush_log(sizeof($genotypes).' SNP genotypes available.', 'success'); }
    else { return drush_set_error('Unable to find or insert genotypes.'); }

/*
    $options = array('num' => 3, 'generate germplasm' => TRUE);
    $stocks = generate_nd_samples($options);
    print_r($stocks);

    $stocks = generate_chado_stock(
      array('name' => '[name] Extraction', 'uniquename' => '[first]_[last]_extraction', 'type_id' => $sample_type_id, 'organism_id' => $organism_id),
      array('num' => 2, 'country' => 'england')
    );
    print_r($stocks);
 */
  }
  catch (Exception $e) {
    $transaction->rollback();
    watchdog_exception('my_type', $e);
  }

  $transaction->rollback();
}
