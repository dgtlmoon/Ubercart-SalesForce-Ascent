<?php

/**
 * Registers the order line item with salesforce
 * @param salesforce interface $sf
 * @param object of each order product from $order $order_product
 */
function _uc_ascent_salesforce_salesforce_enter_order_line($sf, $soName, $order_product) {
  //addSOLine(string soName,string itemName,double price, double quantity,double discount)
  $id = _uc_ascent_query_table($sf, 'PBSI__PBSI_item__c', $order_product->model );
	if(!$id) {
		return false;
	}

	if ($result = $sf->addSalesOrderLineItem($soName, $id , $order_product->price, $order_product->qty, 0.00)) {
    return $result;
  }
  
  
  return false;
}

function _uc_ascent_salesforce_salesforce_enter($sf, $order, $sf_account_id, $sf_contact_id) {
  
  $contactName = $order->delivery_first_name . ' ' . $order->delivery_last_name;
  $shippingInstructions = _uc_ascent_get_all_order_comments($order->order_id);

  // SalesTax per Teresa's rules
  // Items are sales tax free, except in australia
  // where the shipping amount attracts a 10% tax
  $shipping_total = 0.00; // now calculated from the line entry
  $salesTax = 0.00;  
    
  if($order->currency == 'AUD') {
    // shipping total includes 10% of the tax
    // so record that
    $salesTax = 10;
  }

  // the API accepts discount as a % not an arbitrary number
  // work out what % this discount is from the total amount of the order
  
  $result = $sf->addSalesOrder( $sf_account_id, 
                                    $sf_contact_id, 
                                    $shippingInstructions, 
                                    $shipping_total, 
                                    $salesTax 
                                    );
     
  return $result;
}


/**
 * Update various fields for a particular salesorder
 * @param string $soID
 * @param object $order
 */
function _uc_ascent_salesforce_salesorder_update_aux_fields($sf, $soID, $order) {
  
  try {
    if ($result = $sf->queryAll("Select PBSI__Order_Date__c from PBSI__PBSI_Sales_Order__c where Id = '{$soID}'")) {
      if (count($result->records) == 0) {
        uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
        uc_ascent_report_error($sf, 'Query SalesForce table', "$soID does not exist in SalesForce/Ascent table Sales Orders");
        return false;
      }
    }
  
  }
  catch (SoapFault $fault) {
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
    return false;
  }
  
  uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
  
  $original_date = $result->records[0]->fields->PBSI__Order_Date__c;
  
  $sObject1 = new stdClass();
  $sObject1->Id = $soID;  
  $sObject1->PBSI__Customer_Purchase_Order__c = "Weborder #".$order->order_id; //maximum 15 chars?
  $sObject1->Payment_Status__c = "Paid";
  $sObject1->PBSI__Due_Date__c = $original_date;
  $sObject1->Required_Delivery_Date__c = $original_date;
  $sObject1->PBSI__Invoice_Date__c = $original_date;

  // new method of tellign ascent about a discount, lodge them as a negative misc charge (crap)
  $sObject1->PBSI__Misc_Charges__c = uc_ascent_salesforce_total_lineitem_type($order, "coupon");
  
  try {
    
    if ( $updateResponse = $sf->update(array($sObject1),"PBSI__PBSI_Sales_Order__c") ) {
      uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
      return $updateResponse[0]->success;
    }
  
  } catch (SoapFault $fault) {
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
    return false;
  }
    
  return false;
                                
}


function _uc_ascent_get_all_order_comments($order_id) {
  $result = db_query("SELECT message FROM {uc_order_comments} WHERE order_id = %d ORDER BY created DESC", $order_id);
  $output = "";
  
  while ($row = db_fetch_array($result)) {
    $output .= $row ['message'] . "\n";
  }
  
  return $output;

}
function _uc_ascent_get_order_total($order_id) {
  return db_result(db_query("SELECT sum(amount) FROM {uc_order_line_items} WHERE order_id =%d", $order_id));
}

function _uc_ascent_query_table($sf, $key, $value) {
	static $ids;	
	
	if($ids[$key.$value]) {
		return $ids[$key.$value];
	}
	try {
    if ( $result = $sf->queryAll("Select Id from {$key} where Name = '{$value}'") ) {
      if(count($result->records) == 0) {
  	     uc_ascent_report_error($sf, 'Query SalesForce table', "$value does not exist in SalesForce/Ascent table $key");
      }
    	$ids[$key.$value] = $result->records[0]->Id;
    } 

  } catch (SoapFault $fault) {
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
    return false;
  }
  
  uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
  return $ids[$key.$value];
	
}

function _uc_ascent_get_shippinglineitem_product_code() {
  return 'Freight CP Domestic';
}

/**
 * update all the other parameters on a given account/contact ID pair
 * @param sInterface $sf
 * @param sfObject $accountID
 * @param sfObject $contactID
 * @param ubercartOrder $order
 */
function _uc_ascent_salesforce_set_account_aux($sf, $accountId, $contactId, $order) {
  
  // people have a nasty habbit of changing things
  //$lookups['EXP'] =_uc_ascent_query_table($sf, "c2g__codaTaxCode__c", "EXP"); //@TODO not sure about this one
  $lookups['FRE'] =_uc_ascent_query_table($sf, "c2g__codaTaxCode__c", "FRE");
  $lookups['Accounts Payable'] = _uc_ascent_query_table($sf, "c2g__codaGeneralLedgerAccount__c", "Accounts Payable");
  $lookups['Accounts Receivable'] = _uc_ascent_query_table($sf, "c2g__codaGeneralLedgerAccount__c", "Accounts Receivable");  
  $lookups['Consumer Products']  = _uc_ascent_query_table($sf, "c2g__CODADimension1__c", "Consumer Products");

  foreach($lookups as $field=> $lookup) {
    if(!strlen($lookup)) {
      uc_ascent_report_error($sf, 'Account checked failed', $field. " field value is missing");
      return false;
    }      
  }

  $delivery_zone = uc_get_zone_code($order->delivery_zone);
  $delivery_country = uc_get_country_data(array (
    'country_id' => $order->delivery_country 
  ));
  $billing_zone = uc_get_zone_code($order->billing_zone);
  $billing_country = uc_get_country_data(array (
    'country_id' => $order->billing_country 
  ));
  
  // update the Account , create the update object
  
  $sObject1 = new stdClass();
  $sObject1->Id = $accountId;
  $sObject1->BillingCity = $order->billing_city ;
  $sObject1->BillingCountry = $billing_country [0] ['country_name'] ; 
  $sObject1->BillingPostalCode = $order->billing_postal_code ; 
  $sObject1->BillingState = $billing_zone ;
  $sObject1->BillingStreet = $order->billing_street1 . ' ' . $order->billing_street2 ; 
  $sObject1->Phone = $order->billing_phone ; 
  $sObject1->ShippingCity = $order->billing_city ; 
  $sObject1->ShippingState = $delivery_zone ; 
  $sObject1->ShippingCountry = $delivery_country [0] ['country_name'] ; 
  $sObject1->ShippingPostalCode = $order->delivery_postal_code ; 
  $sObject1->ShippingStreet = $order->delivery_street1 . ' ' . $order->delivery_street2 ;

  // Extra custom fields
  //if you change the parameters in _uc_ascent_query_table calls make sure to change the tests in the admin check
  $sObject1->PBSI__Payment_Terms__c = 'Cash';
  $sObject1->Current_Payment_Terms__c = 'Cash';
  $sObject1->c2g__CODAAccountsPayableControl__c = $lookups['Accounts Payable'];
  $sObject1->c2g__CODAAccountsReceivableControl__c = $lookups['Accounts Receivable'];
  $sObject1->c2g__CODAVATStatus__c = 'Home';
  $sObject1->c2g__CODADimension1__c = $lookups['Consumer Products'];
  $sObject1->c2g__CODASalesTaxStatus__c = 'Exempt from Qualifying Products';      
  $sObject1->c2g__CODATaxCalculationMethod__c = 'Gross';
  $sObject1->c2g__CODAECCountryCode__c = 'GB'; 

  
  switch($order->currency) {
    case 'AUD':
      $sObject1->c2g__CODATaxCode1__c = $lookups['FRE'];
      $sObject1->c2g__CODAInputVATCode__c =  $lookups['FRE'];
      $sObject1->c2g__CODAOutputVATCode__c = $lookups['FRE'];
      $sObject1->CurrencyIsoCode = 'AUD';           
    break;
    case 'USD':        
      //$sObject1->c2g__CODATaxCode1__c = $lookups['EXP'];
      //$sObject1->c2g__CODAInputVATCode__c =  $lookups['EXP'];
      //$sObject1->c2g__CODAOutputVATCode__c = $lookups['EXP'];
      $sObject1->CurrencyIsoCode = 'USD';                   
    break;      
  }
  
  try {
    if ( $updateResponse = $sf->update(array($sObject1), "Account" ) ) {
      uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
      if(!$updateResponse[0]->success) {
        return false;
      }
    } else {
      return false;
    }
  
  } catch (SoapFault $fault) {
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
    return false;
  }
  
  // create the contact update object
  $sObject1 = new stdClass();
  $sObject1->Id = $contactId;
  $sObject->Homephone = $order->delivery_phone; 
  $sObject->MailingCity = $order->delivery_city; 
  $sObject->MailingState = $delivery_zone;
  $sObject->MailingCountry = $delivery_country [0] ['country_name']; 
  $sObject->MailingPostalCode = $order->delivery_postal_code;
  $sObject->MailingStreet = $order->delivery_street1 . ' ' . $order->delivery_street2; 
  $sObject->MobilePhone = $order->delivery_phone; 
  $sObject->Customer_Type__c = 'Consumer Direct: Online';
  try {
    if ( $updateResponse = $sf->update(array($sObject1), "Contact" ) ) {
      uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
      return $updateResponse[0]->success;
    }
  
  } catch (SoapFault $fault) {
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
    return false;
  }
    
  return false;
}
/**
 * Upsert a very primitive base record
 * this could be calle from webform type, or could be a primer for later updates
 * 
 * delivery details are Contact
 * billing details are Account
 * 
 * @return the accountID && contactID of this person in SF
 */
function _uc_ascent_salesforce_account_upsert_contact($sf, $billingFirstName, $billingLastName, 
                                                           $deliveryFirstName, $deliveryLastName,
                                                           $deliveryEmail, $billingEmail, 
                                                           $description="Added from website",
                                                           $billingPhone ="", $deliveryPhone="") {
 
  // check for a Contact that exists with this delivery
  
  // make sure we escape strings in this query, these don't get automatically escaped
  $q_deliveryFirstName = addslashes(ucfirst(strtolower(trim($deliveryFirstName))));
  $q_deliveryLastName = addslashes(ucfirst(strtolower(trim($deliveryLastName))));
  
  // sometimes staff enter sales for a customer, and dont get their email address
  try {
    if(strlen($deliveryEmail)) {
      $result = $sf->queryAll("SELECT AccountId, Id, FirstName, LastName FROM Contact WHERE 
                                FirstName = '{$q_deliveryFirstName}' 
                                AND LastName = '{$q_deliveryLastName}'
                                AND Email = '{$deliveryEmail}'");
    } else {
      $result = $sf->queryAll("SELECT AccountId, Id, FirstName, LastName FROM Contact WHERE 
                                FirstName = '{$q_deliveryFirstName}' 
                                AND LastName = '{$q_deliveryLastName}'");    
    }
  } catch (SoapFault $fault) {      
     uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
     return false;      
  }
  
  uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
    
  // if no contact was matched
  // create an Account then the Contact with the new accountID to link it
  if ($result->size == 0) {
    
    // create the Account for this
    $record = array (
      'Type' => 'Customer',
      'Description' => $description,
      'Phone' => $billingPhone,
      'Name' => ucfirst(strtolower($billingFirstName)).' '.ucfirst(strtolower($billingLastName)),
    );
    try {
      $response = $sf->create(array (
        $record 
      ), 'Account');
    } catch (SoapFault $fault) {      
      uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
      return false;      
    }
  
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
   
    
    if(! $return['accountId'] = $response[0]->id) {
      uc_ascent_report_error($sf, 'Creating account failed', "Creating account failed");
      return false;      
    }
    // now the contact (actual Delivery details)
    $record = array (
      'Description' => $description,
      'FirstName' => ucfirst(strtolower($deliveryFirstName)), 
      'LastName' => ucfirst(strtolower($deliveryLastName)),    
      'Email' => $deliveryEmail, 
      'HomePhone' => $billingPhone,    
      'MobilePhone' => $billingPhone,
      'AccountId' => $response [0]->id,
    );
    try {
      $response = $sf->create(array (
        $record 
      ), 'Contact');
      $return ['contactId'] = $response [0]->id;
      
    } catch (SoapFault $fault) {      
         uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), false);
         return false;      
    }
      
    uc_ascent_record_queue(false, $sf->sforce->__getLastRequest(), $sf->sforce->__getLastResponse(), true);
   
  }
  else {
    $return ['accountId'] = $result->records[0]->fields->AccountId;
    $return ['contactId'] = $result->records[0]->fields->Id;
  }
  
  return $return;
}

class AscentSFClient extends SforceBaseClient {
  const ENTERPRISE_NAMESPACE = 'urn:partner.soap.sforce.com';
  public $sforce; // like the stock version but public so we can access more debug
  
  function AscentSFClient() {
    $this->namespace = self::ENTERPRISE_NAMESPACE;
  }
  
  function createAscentConnection($ascent_wsdl, $proxy = null) {
    
    $soapClientArray = array (
      'user_agent' => 'salesforce-toolkit-php/' . $this->version, 'encoding' => 'utf-8', 'trace' => 1, 'compression' => SOAP_COMPRESSION_ACCEPT | SOAP_COMPRESSION_GZIP 
    );
    
    if ($proxy != null) {
      $proxySettings = array ();
      $proxySettings ['proxy_host'] = $proxy->host;
      $proxySettings ['proxy_port'] = $proxy->port; // Use an integer, not a string
      $proxySettings ['proxy_login'] = $proxy->login;
      $proxySettings ['proxy_password'] = $proxy->password;
      $soapClientArray = array_merge($soapClientArray, $proxySettings);
    }
    
    $this->ascent = new SoapClient($ascent_wsdl, $soapClientArray);
  }
  
  /**
   * Adds one or more new individual objects to your organization's data.
   * @param array $sObjects    Array of one or more sObjects (up to 200) to create.
   * @param AssignmentRuleHeader $assignment_header is optional.  Defaults to NULL
   * @param MruHeader $mru_header is optional.  Defaults to NULL
   * @return SaveResult
   */
  public function create($sObjects, $type) {
    foreach ($sObjects as &$sObject) {
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #1)
      $xmlStr = '';
      if (isset($sObject->fieldsToNull) && is_array($sObject->fieldsToNull)) {
        foreach ($sObject->fieldsToNull as $fieldToNull) {
          $xmlStr .= '<fieldsToNull>' . $fieldToNull . '</fieldsToNull>';
        }
      }
      // ------
      

      $sObject = new SoapVar($sObject, SOAP_ENC_OBJECT, $type, $this->namespace);
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #2)
      if ($xmlStr != '') {
        $sObject->enc_value->fieldsToNull = new SoapVar(new SoapVar($xmlStr, XSD_ANYXML), SOAP_ENC_ARRAY);
      }
      // ------
    }
    $arg = $sObjects;
    
    return parent::_create(new SoapParam($arg, "sObjects"));
  }
  
  /**
   * Updates one or more new individual objects to your organization's data.
   * @param array sObjects    Array of sObjects
   * @param AssignmentRuleHeader $assignment_header is optional.  Defaults to NULL
   * @param MruHeader $mru_header is optional.  Defaults to NULL
   * @return UpdateResult
   */
  public function update($sObjects, $type, $assignment_header = NULL, $mru_header = NULL) {
    
    foreach ($sObjects as &$sObject) {
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #1)
      $xmlStr = '';
      if (isset($sObject->fieldsToNull) && is_array($sObject->fieldsToNull)) {
        foreach ($sObject->fieldsToNull as $fieldToNull) {
          $xmlStr .= '<fieldsToNull>' . $fieldToNull . '</fieldsToNull>';
        }
      }
      // ------
      

      $sObject = new SoapVar($sObject, SOAP_ENC_OBJECT, $type, $this->namespace);
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #2)
      if ($xmlStr != '') {
        $sObject->enc_value->fieldsToNull = new SoapVar(new SoapVar($xmlStr, XSD_ANYXML), SOAP_ENC_ARRAY);
      }
      // ------
    }
    $arg = new stdClass();
    $arg->sObjects = $sObjects;
    return parent::_update($arg);
  }
  
  /**
   * Creates new objects and updates existing objects; uses a custom field to
   * determine the presence of existing objects. In most cases, we recommend
   * that you use upsert instead of create because upsert is idempotent.
   * Available in the API version 7.0 and later.
   *
   * @param string $ext_Id External Id
   * @param array  $sObjects Array of sObjects
   * @param string $type The type of objects being upserted.
   * @return UpsertResult
   */
  public function upsert($ext_Id, $sObjects, $type = 'Contact') {
    $arg = new stdClass();
    $arg->externalIDFieldName = new SoapVar($ext_Id, XSD_STRING, 'string', 'http://www.w3.org/2001/XMLSchema');
    foreach ($sObjects as &$sObject) {
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #1)
      $xmlStr = '';
      if (isset($sObject->fieldsToNull) && is_array($sObject->fieldsToNull)) {
        foreach ($sObject->fieldsToNull as $fieldToNull) {
          $xmlStr .= '<fieldsToNull>' . $fieldToNull . '</fieldsToNull>';
        }
      }
      // ------
      

      $sObject = new SoapVar($sObject, SOAP_ENC_OBJECT, $type, $this->namespace);
      
      // FIX for fieldsToNull issue - allow array in fieldsToNull (STEP #2)
      if ($xmlStr != '') {
        $sObject->enc_value->fieldsToNull = new SoapVar(new SoapVar($xmlStr, XSD_ANYXML), SOAP_ENC_ARRAY);
      }
      // ------
    }
    $arg->sObjects = $sObjects;
    return parent::_upsert($arg);
  }
  
  /**
   * Merge records
   *
   * @param stdclass $mergeRequest
   * @param String $type
   * @return unknown
   */
  public function merge($mergeRequest, $type) {
    $mergeRequest->masterRecord = new SoapVar($mergeRequest->masterRecord, SOAP_ENC_OBJECT, $type, $this->namespace);
    $arg = new stdClass();
    $arg->request = new SoapVar($mergeRequest, SOAP_ENC_OBJECT, 'MergeRequest', $this->namespace);
    return parent::_merge($arg);
  }
  
  /**
   * <xsd:element name="addSOLine">
   * <xsd:complexType>
   * <xsd:sequence>
   * <xsd:element name="soId" type="xsd:string" nillable="true"/>
   * <xsd:element name="itemId" type="xsd:string" nillable="true"/>
   * <xsd:element name="price" type="xsd:double" nillable="true"/>
   * <xsd:element name="quantity" type="xsd:double" nillable="true"/>
   * <xsd:element name="discount" type="xsd:double" nillable="true"/>
   * </xsd:sequence>
   * </xsd:complexType>
   * </xsd:element>
   * 
   */
  
  public function addSalesOrderLineItem($soId, $itemId, $price, $quantity) {
    $this->_setSessionHeader($this->ascent);
    
    try {
      
      $values = array (
        'soId' => $soId, 
        'itemId' => $itemId, 
        'price' => $price, //@todo each is it?
        'quantity' => $quantity, 
        'discount' => 0 
      );
      
      $result = $this->ascent->addSOLine($values);
    
    }
    catch (SoapFault $fault) {      
      uc_ascent_record_queue(false, $this->ascent->__getLastRequest(), $this->ascent->__getLastResponse(), false);      
    }
    
    uc_ascent_record_queue(false, $this->ascent->__getLastRequest(), $this->ascent->__getLastResponse(), true);
    
    return $result->result;
  }
  
  /**
   * add a SalesOrder to Ascent API
   * 
   * global id addSO(string AccountID,
   * string ContactID,
   * String shippingInstructions,
   * Double ShippingCost,
   * Double salesTax,
   * double discount)
   * From WSDL
   * <xsd:element name="addSO">
   * <xsd:complexType>
   * <xsd:sequence>
   * <xsd:element name="AccountId" type="xsd:string" nillable="true"/>
   * <xsd:element name="contactId" type="xsd:string" nillable="true"/>
   * <xsd:element name="shippingInstructions" type="xsd:string" nillable="true"/>
   * <xsd:element name="ShippingCost" type="xsd:double" nillable="true"/>
   * <xsd:element name="salesTax" type="xsd:double" nillable="true"/>
   * <xsd:element name="discount" type="xsd:double" nillable="true"/>
   * </xsd:sequence>
   * </xsd:complexType>
   * </xsd:element>
   */
  public function addSalesOrder($accountId, $contactId, $shippingInstructions, $shippingCost=0.00, $salesTax =0.00) {
    
    $this->_setSessionHeader($this->ascent);
    
    try {      
      $values = array (
        'AccountId' => $accountId, 
        'contactId' => $contactId, 
        'shippingInstructions' => strip_tags($shippingInstructions), 
        'ShippingCost' => $shippingCost, 
        'salesTax' => $salesTax, 
        'discount' => 0, // discount should always be 0 because now its handled as PBSI__Misc_Charges__c 
      );
      
      $result = $this->ascent->addSO($values);    
    }
    catch (SoapFault $fault) {
      uc_ascent_record_queue(false, $this->ascent->__getLastRequest(), $this->ascent->__getLastResponse(), false);
      return false;
    }
    
    uc_ascent_record_queue(false, $this->ascent->__getLastRequest(), $this->ascent->__getLastResponse(), true);
    
    // @todo: update any additional fields here?
    return $result->result;
  }
  
  private function setHeaders($call = NULL) {
    $this->sforce->__setSoapHeaders(NULL);
    
    $header_array = array (
      $this->sessionHeader 
    );
    
    $header = $this->callOptions;
    if ($header != NULL) {
      array_push($header_array, $header);
    }
    
    if ($call == "create" || $call == "merge" || $call == "update" || $call == "upsert") {
      $header = $this->assignmentRuleHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "login") {
      $header = $this->loginScopeHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "create" || $call == "resetPassword" || $call == "update" || $call == "upsert") {
      $header = $this->emailHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "create" || $call == "merge" || $call == "query" || $call == "retrieve" || $call == "update" || $call == "upsert") {
      $header = $this->mruHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "delete") {
      $header = $this->userTerritoryDeleteHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    if ($call == "query" || $call == "queryMore" || $call == "retrieve") {
      $header = $this->queryHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    // try to add allowFieldTruncationHeader
    $allowFieldTruncationHeaderCalls = array (
      'convertLead', 'create', 'merge', 'process', 'undelete', 'update', 'upsert' 
    );
    if (in_array($call, $allowFieldTruncationHeaderCalls)) {
      $header = $this->allowFieldTruncationHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    // try to add localeOptions
    if ($call == 'describeSObject' || $call == 'describeSObjects') {
      $header = $this->localeOptions;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    // try to add PackageVersionHeader
    $packageVersionHeaderCalls = array (
      'convertLead', 'create', 'delete', 'describeGlobal', 'describeLayout', 'describeSObject', 'describeSObjects', 'describeSoftphoneLayout', 'describeTabs', 'merge', 'process', 'query', 'retrieve', 'search', 'undelete', 'update', 'upsert' 
    );
    if (in_array($call, $packageVersionHeaderCalls)) {
      $header = $this->packageVersionHeader;
      if ($header != NULL) {
        array_push($header_array, $header);
      }
    }
    
    $this->sforce->__setSoapHeaders($header_array);
  
  }
  
  function _setSessionHeader(&$soapInterface) {
    $strHeaderComponent_Session = "<SessionHeader><sessionId>{$this->sessionId}</sessionId></SessionHeader>";
    $objVar_Session_Inside = new SoapVar($strHeaderComponent_Session, XSD_ANYXML, null, null, null);
    $objHeader_Session_Outside = new SoapHeader('http://soap.sforce.com/schemas/class/PBSI/Ascent_API', 'SessionHeader', $objVar_Session_Inside);
    $soapInterface->__setSoapHeaders(array (
      $objHeader_Session_Outside 
    ));
  }

}

